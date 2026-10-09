"""Validate measured service-host policy and WinRT registration blockers.

A pass validates the recorded failures; it is not a running-service or install
success assertion.
"""
import argparse
import hashlib
from pathlib import Path


def check(root: Path) -> None:
    policy = (root / "package-manager-service-lab-policy-test.log").read_text(
        encoding="utf-8", errors="replace"
    )
    assert "REAL_SET_SERVICE_DACL=1 ERROR=0" in policy
    assert "REAL_QUERY_SERVICE_DACL=1 ERROR=0" in policy
    assert "ACTUAL_SERVICE_DACL=D:" in policy
    assert "EXPECTED_SERVICE_DACL=D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)" in policy
    assert "GENUINE_SERVICE_DACL_ROUNDTRIP=FAIL" in policy
    registration = (root / "package-manager-service-lab-registration-test.log").read_text(
        encoding="utf-8", errors="replace"
    )
    assert "DIRECT_GENUINE_FACTORY_HRESULT=0x00000000" in registration
    assert "REAL_RO_REGISTER_HRESULT=0x00000000 COOKIE_CHANGED=0" in registration
    assert "REAL_REGISTERED_FACTORY_LOOKUP=0x80040154 GENUINE_CALLBACK_CALLS=0 OBJECT=absent" in registration
    activation = (root / "package-manager-service-lab-activation.log").read_text(
        encoding="utf-8", errors="replace"
    )
    assert "REAL_INTERNAL_SERVICE_FACTORY_HRESULT=0x80040150" in activation
    status = (root / "package-manager-service-lab-status-before.log").read_text(
        encoding="utf-8", errors="replace"
    )
    assert "REAL_SERVICE_STATE=1 PID=0 WIN32_EXIT=1077" in status
    provider = root / "package-manager-service-lab-state-helper.dll"
    assert hashlib.sha256(provider.read_bytes()).hexdigest() == (
        "17172e2eb646fd630d7f6df7d127dbf0995ff765fa908e8dc80fef84962dda71"
    )
    print("MEASURED_BLOCK=SCM_SECURITY_POLICY_NOT_PRESERVED")
    print("MEASURED_BLOCK=WINRT_REGISTRATION_COOKIE_AND_CALLBACK_TRANSPORT_ABSENT")
    print("PACKAGE_MANAGER_SERVICE_HOST_EVIDENCE_PASS (service not started; no activation success)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence_directory", type=Path)
    check(parser.parse_args().evidence_directory)
