use std::collections::BTreeMap;
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Write};
use std::path::{Component, Path, PathBuf};

use chrono::Utc;
use fs2::FileExt;
use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::wire::*;

pub const MAX_JOBS: usize = 256;
pub const MAX_EVENTS: usize = 4096;
pub const MAX_PRODUCTS: usize = 512;
const MAX_STATE_BYTES: u64 = 32 * 1024 * 1024;

#[derive(Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct DurableState {
    pub version: Protocol,
    #[serde(rename = "sessionID")]
    pub session_id: String,
    pub watermark: u64,
    pub jobs: BTreeMap<String, Job>,
    pub idempotency: BTreeMap<String, String>,
    pub events: Vec<Event>,
    pub catalog: BTreeMap<String, ProductRecord>,
    pub cache_revision: u64,
}

pub struct Store {
    directory: PathBuf,
    _lock: File,
    pub state: DurableState,
}

pub fn now() -> String {
    Utc::now().to_rfc3339_opts(chrono::SecondsFormat::Secs, true)
}

fn recovery() -> WireError {
    WireError::new(
        ErrorCode::RegistryRecoveryRequired,
        "Management state is unavailable or invalid. Preserve it and review local recovery diagnostics.",
        false,
    )
}

fn increment(value: u64) -> Result<u64, WireError> {
    value.checked_add(1).ok_or_else(recovery)
}

pub fn identifier_valid(value: &str) -> bool {
    !value.is_empty() && value.chars().count() <= 128 && !value.chars().any(char::is_control)
}

pub fn catalog_key(product: &ProductParams) -> String {
    format!(
        "{}:{}:{}",
        product.product_id, product.market, product.language
    )
}

impl Store {
    pub fn open(directory: &Path) -> Result<Self, WireError> {
        validate_directory(directory)?;
        let lock_path = directory.join("management.lock");
        regular_or_missing(&lock_path)?;
        let mut options = OpenOptions::new();
        options.create(true).read(true).write(true).truncate(false);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.mode(0o600);
        }
        let lock = options.open(&lock_path).map_err(|_| recovery())?;
        lock.try_lock_exclusive().map_err(|_| {
            WireError::new(
                ErrorCode::StateLocked,
                "Another management process owns this state directory.",
                true,
            )
        })?;
        let state_path = directory.join("management.json");
        regular_or_missing(&state_path)?;
        let state = match File::open(&state_path) {
            Ok(file) => {
                if file.metadata().map_err(|_| recovery())?.len() > MAX_STATE_BYTES {
                    return Err(recovery());
                }
                let mut bytes = Vec::new();
                file.take(MAX_STATE_BYTES + 1)
                    .read_to_end(&mut bytes)
                    .map_err(|_| recovery())?;
                let mut value: serde_json::Value =
                    serde_json::from_slice(&bytes).map_err(|_| recovery())?;
                if let Some(catalog) = value
                    .get_mut("catalog")
                    .and_then(serde_json::Value::as_object_mut)
                {
                    for product in catalog.values_mut() {
                        let fields = product.as_object_mut().ok_or_else(recovery)?;
                        if !fields.contains_key("artwork") && !fields.contains_key("artworkStatus")
                        {
                            fields.insert("artwork".to_owned(), serde_json::json!([]));
                            fields.insert(
                                "artworkStatus".to_owned(),
                                serde_json::json!("notQueried"),
                            );
                        }
                    }
                }
                let state: DurableState = serde_json::from_value(value).map_err(|_| recovery())?;
                validate_state(&state)?;
                state
            }
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => DurableState {
                version: Protocol::default(),
                session_id: Uuid::new_v4().to_string(),
                watermark: 0,
                jobs: BTreeMap::new(),
                idempotency: BTreeMap::new(),
                events: Vec::new(),
                catalog: BTreeMap::new(),
                cache_revision: 0,
            },
            Err(_) => return Err(recovery()),
        };
        let mut store = Self {
            directory: directory.to_owned(),
            _lock: lock,
            state,
        };
        store.persist(&store.state)?;
        let mut recovered = store.state.clone();
        let interrupted: Vec<_> = recovered
            .jobs
            .values()
            .filter(|job| !job.state.terminal())
            .map(|job| job.job_id.clone())
            .collect();
        for id in interrupted {
            let job = recovered.jobs.get_mut(&id).ok_or_else(recovery)?;
            job.state = JobState::Failed;
            job.error = Some(WireError::new(
                ErrorCode::NetworkUnavailable,
                "Refresh was interrupted. Retry explicitly when network access is available.",
                true,
            ));
            job.revision = increment(job.revision)?;
            job.updated_at = now();
            let job = job.clone();
            append_event(&mut recovered, &job)?;
        }
        store.commit(recovered)?;
        Ok(store)
    }

    fn persist(&self, state: &DurableState) -> Result<(), WireError> {
        regular_or_missing(&self.directory.join("management.json"))?;
        let bytes = serde_json::to_vec(state).map_err(|_| recovery())?;
        if bytes.len() as u64 > MAX_STATE_BYTES {
            return Err(WireError::new(
                ErrorCode::LimitExceeded,
                "Management state capacity reached. Preserve the directory and choose a new private scope.",
                false,
            ));
        }
        let mut temporary =
            tempfile::NamedTempFile::new_in(&self.directory).map_err(|_| recovery())?;
        temporary.write_all(&bytes).map_err(|_| recovery())?;
        temporary.as_file().sync_all().map_err(|_| recovery())?;
        temporary
            .persist(self.directory.join("management.json"))
            .map_err(|_| recovery())?;
        #[cfg(unix)]
        File::open(&self.directory)
            .and_then(|file| file.sync_all())
            .map_err(|_| recovery())?;
        Ok(())
    }

    fn commit(&mut self, state: DurableState) -> Result<(), WireError> {
        self.persist(&state)?;
        self.state = state;
        Ok(())
    }

    pub fn enqueue(
        &mut self,
        request_id: &str,
        key: &str,
        product: ProductParams,
    ) -> Result<(Job, bool), WireError> {
        if !identifier_valid(key) {
            return Err(WireError::new(
                ErrorCode::InvalidRequest,
                "Invalid idempotency key.",
                false,
            ));
        }
        if let Some(id) = self.state.idempotency.get(key) {
            let job = self.state.jobs.get(id).ok_or_else(recovery)?;
            if job.product != product {
                return Err(WireError::new(
                    ErrorCode::IdempotencyConflict,
                    "This idempotency key belongs to different refresh parameters.",
                    false,
                ));
            }
            return Ok((job.clone(), false));
        }
        if self.state.jobs.len() >= MAX_JOBS {
            return Err(WireError::new(
                ErrorCode::LimitExceeded,
                "Job capacity reached; no durable job was evicted.",
                false,
            ));
        }
        let timestamp = now();
        let job = Job {
            job_id: Uuid::new_v4().to_string(),
            revision: 1,
            kind: JobKind::CatalogRefresh,
            state: JobState::Queued,
            request_id: request_id.to_owned(),
            created_at: timestamp.clone(),
            updated_at: timestamp,
            product,
            attempt: 1,
            error: None,
        };
        let mut next = self.state.clone();
        next.idempotency.insert(key.to_owned(), job.job_id.clone());
        next.jobs.insert(job.job_id.clone(), job.clone());
        append_event(&mut next, &job)?;
        self.commit(next)?;
        Ok((job, true))
    }

    pub fn transition(
        &mut self,
        id: &str,
        state: JobState,
        error: Option<WireError>,
        product: Option<ProductRecord>,
    ) -> Result<Job, WireError> {
        let mut next = self.state.clone();
        let job = next.jobs.get_mut(id).ok_or_else(|| {
            WireError::new(
                ErrorCode::NotFound,
                "Job does not exist in this management scope.",
                false,
            )
        })?;
        let allowed = matches!(
            (&job.state, &state),
            (
                JobState::Queued,
                JobState::Running | JobState::Cancelled | JobState::Failed
            ) | (
                JobState::Running,
                JobState::Completed | JobState::Failed | JobState::Cancelled
            )
        );
        if !allowed {
            return Err(WireError::new(
                ErrorCode::InvalidTransition,
                "Job cannot enter this state; terminal jobs cannot be revived.",
                false,
            ));
        }
        if state == JobState::Completed {
            let record = product.ok_or_else(recovery)?;
            if record.product_id != job.product.product_id
                || record.market != job.product.market
                || record.language != job.product.language
            {
                return Err(recovery());
            }
            let key = catalog_key(&job.product);
            if !next.catalog.contains_key(&key) && next.catalog.len() >= MAX_PRODUCTS {
                return Err(WireError::new(
                    ErrorCode::LimitExceeded,
                    "Public metadata cache capacity reached.",
                    false,
                ));
            }
            next.catalog.insert(key, record);
            next.cache_revision = increment(next.cache_revision)?;
        } else if product.is_some() {
            return Err(recovery());
        }
        job.state = state;
        job.error = error;
        job.revision = increment(job.revision)?;
        job.updated_at = now();
        let job = job.clone();
        append_event(&mut next, &job)?;
        self.commit(next)?;
        Ok(job)
    }

    pub fn cache_product(&mut self, record: ProductRecord) -> Result<(), WireError> {
        let key = format!(
            "{}:{}:{}",
            record.product_id, record.market, record.language
        );
        if !self.state.catalog.contains_key(&key) && self.state.catalog.len() >= MAX_PRODUCTS {
            return Err(WireError::new(
                ErrorCode::LimitExceeded,
                "Public metadata cache capacity reached.",
                false,
            ));
        }
        let mut next = self.state.clone();
        next.catalog.insert(key, record);
        next.cache_revision = increment(next.cache_revision)?;
        self.commit(next)
    }

    pub fn cache_discovery_products(&mut self, records: &[ProductRecord]) -> Result<(), WireError> {
        if records.is_empty() || records.len() > 16 {
            return Err(recovery());
        }
        let mut next = self.state.clone();
        let mut page_keys = std::collections::BTreeSet::new();
        for record in records {
            let key = format!(
                "{}:{}:{}",
                record.product_id, record.market, record.language
            );
            page_keys.insert(key.clone());
            next.catalog.insert(key, record.clone());
        }
        // Discovery is a bounded public cache, never an ownership/installation registry.
        while next.catalog.len() > MAX_PRODUCTS {
            let oldest = next
                .catalog
                .iter()
                .filter(|(key, _)| !page_keys.contains(*key))
                .min_by_key(|(_, record)| &record.checked_at)
                .map(|(key, _)| key.clone())
                .ok_or_else(recovery)?;
            next.catalog.remove(&oldest);
        }
        next.cache_revision = increment(next.cache_revision)?;
        self.commit(next)
    }

    pub fn checked_job(&self, mutation: &JobMutation) -> Result<Job, WireError> {
        let job = self.state.jobs.get(&mutation.job_id).ok_or_else(|| {
            WireError::new(
                ErrorCode::NotFound,
                "Job does not exist in this management scope.",
                false,
            )
        })?;
        if mutation.expected_revision != job.revision {
            return Err(WireError::new(
                ErrorCode::RevisionConflict,
                "Job changed. Refresh its snapshot before attempting a mutation.",
                true,
            ));
        }
        Ok(job.clone())
    }

    pub fn retry(&mut self, mutation: &JobMutation) -> Result<Job, WireError> {
        let mut job = self.checked_job(mutation)?;
        if job.state != JobState::Failed
            || job.attempt >= 3
            || !job.error.as_ref().is_some_and(|error| error.retryable)
        {
            return Err(WireError::new(
                ErrorCode::InvalidTransition,
                "Only retryable failed refreshes with fewer than three attempts can be retried.",
                false,
            ));
        }
        job.state = JobState::Queued;
        job.error = None;
        job.attempt += 1;
        job.revision = increment(job.revision)?;
        job.updated_at = now();
        let mut next = self.state.clone();
        next.jobs.insert(job.job_id.clone(), job.clone());
        append_event(&mut next, &job)?;
        self.commit(next)?;
        Ok(job)
    }

    pub fn replay(&self, params: &ReplayParams) -> Result<ReplayData, WireError> {
        let earliest = self
            .state
            .events
            .first()
            .map_or(self.state.watermark + 1, |event| event.sequence);
        if params.session_id != self.state.session_id
            || params.after_sequence > self.state.watermark
            || params.after_sequence.saturating_add(1) < earliest
        {
            let mut error = WireError::new(
                ErrorCode::EventsExpired,
                "Replay range is unavailable. Apply a new jobs.snapshot before replaying.",
                true,
            );
            error.details = serde_json::json!({"watermark": self.state.watermark,
                "sessionID": self.state.session_id})
            .as_object()
            .cloned();
            return Err(error);
        }
        let events: Vec<_> = self
            .state
            .events
            .iter()
            .filter(|event| event.sequence > params.after_sequence)
            .take(params.limit as usize)
            .cloned()
            .collect();
        let last = events
            .last()
            .map_or(params.after_sequence, |event| event.sequence);
        Ok(ReplayData {
            session_id: self.state.session_id.clone(),
            watermark: self.state.watermark,
            events,
            has_more: last < self.state.watermark,
        })
    }
}

fn append_event(state: &mut DurableState, job: &Job) -> Result<(), WireError> {
    state.watermark = increment(state.watermark)?;
    state.events.push(Event {
        kind: EventKind::Event,
        protocol: Protocol::default(),
        session_id: state.session_id.clone(),
        sequence: state.watermark,
        request_id: job.request_id.clone(),
        job_id: job.job_id.clone(),
        revision: job.revision,
        event: EventName::JobChanged,
        data: job.clone(),
    });
    if state.events.len() > MAX_EVENTS {
        state.events.remove(0);
    }
    Ok(())
}

fn regular_or_missing(path: &Path) -> Result<(), WireError> {
    match fs::symlink_metadata(path) {
        Ok(metadata) if metadata.is_file() && !metadata.file_type().is_symlink() => {
            #[cfg(unix)]
            {
                use std::os::unix::fs::MetadataExt;
                if metadata.nlink() != 1 {
                    return Err(recovery());
                }
            }
            Ok(())
        }
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(()),
        _ => Err(recovery()),
    }
}

pub(crate) fn validate_directory(path: &Path) -> Result<(), WireError> {
    if !path.is_absolute()
        || path
            .components()
            .any(|part| matches!(part, Component::ParentDir | Component::CurDir))
    {
        return Err(WireError::new(
            ErrorCode::InvalidRequest,
            "State directory must be an absolute private path without traversal.",
            false,
        ));
    }
    let mut current = PathBuf::new();
    for component in path.components() {
        current.push(component);
        match fs::symlink_metadata(&current) {
            Ok(metadata) if metadata.is_dir() && !metadata.file_type().is_symlink() => {}
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                let mut builder = fs::DirBuilder::new();
                #[cfg(unix)]
                {
                    use std::os::unix::fs::DirBuilderExt;
                    builder.mode(0o700);
                }
                builder.create(&current).map_err(|_| recovery())?;
            }
            _ => return Err(recovery()),
        }
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        if fs::metadata(path)
            .map_err(|_| recovery())?
            .permissions()
            .mode()
            & 0o777
            != 0o700
        {
            return Err(WireError::new(
                ErrorCode::UnsupportedConfiguration,
                "State directory must have private permissions (0700).",
                false,
            ));
        }
    }
    Ok(())
}

fn validate_state(state: &DurableState) -> Result<(), WireError> {
    if state.version != Protocol::default()
        || !identifier_valid(&state.session_id)
        || state.jobs.len() > MAX_JOBS
        || state.events.len() > MAX_EVENTS
        || state.catalog.len() > MAX_PRODUCTS
        || state.idempotency.len() != state.jobs.len()
        || state.watermark == u64::MAX
    {
        return Err(recovery());
    }
    for (id, job) in &state.jobs {
        if id != &job.job_id
            || !identifier_valid(id)
            || job.revision == 0
            || job.attempt == 0
            || job.attempt > 3
            || !identifier_valid(&job.request_id)
        {
            return Err(recovery());
        }
    }
    for (key, id) in &state.idempotency {
        if !identifier_valid(key) || !state.jobs.contains_key(id) {
            return Err(recovery());
        }
    }
    let mut previous = state
        .events
        .first()
        .map_or(0, |event| event.sequence.saturating_sub(1));
    for event in &state.events {
        if event.sequence != increment(previous)?
            || event.session_id != state.session_id
            || event.protocol != Protocol::default()
            || event.job_id != event.data.job_id
            || event.revision != event.data.revision
            || event.request_id != event.data.request_id
            || !state.jobs.contains_key(&event.job_id)
        {
            return Err(recovery());
        }
        previous = event.sequence;
    }
    if previous != state.watermark {
        return Err(recovery());
    }
    if state
        .catalog
        .values()
        .any(|product| !crate::artwork::valid(&product.artwork, &product.artwork_status))
    {
        return Err(recovery());
    }
    Ok(())
}
