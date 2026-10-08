"""winmd -> widl WinRT IDL (types only, for proxy/stub generation).

usage: python winmd2idl.py out.idl a.winmd [b.winmd ...]
Emits every TypeDef of the given winmds (interfaces, delegates, enums, structs,
runtimeclasses) plus a declare{} block with every generic instantiation used
(and the companion IIterable/IIterator/IKeyValuePair/completed handlers).
"""
import sys, struct, uuid, dnfile

WELL = {
    'System.Guid': 'GUID',
}
PRIM = {0x02: 'boolean', 0x03: 'WCHAR', 0x05: 'BYTE', 0x06: 'INT16', 0x07: 'UINT16',
        0x08: 'INT32', 0x09: 'UINT32', 0x0a: 'INT64', 0x0b: 'UINT64', 0x0c: 'FLOAT',
        0x0d: 'DOUBLE', 0x0e: 'HSTRING', 0x1c: 'IInspectable*'}
GENERIC_KIND = {}  # 'Ns.Name`N' -> 'interface'|'delegate'


class Mod:
    def __init__(self, path):
        self.pe = dnfile.dnPE(path)
        self.md = self.pe.net.mdtables
        self.blob = self.pe.net.blobs if hasattr(self.pe.net, 'blobs') else None


def s(x):
    return str(x) if x is not None else ''


def raw(v):
    if v is None:
        return b''
    if isinstance(v, (bytes, bytearray)):
        return bytes(v)
    if hasattr(v, 'value'):
        vv = v.value
        return bytes(vv) if not isinstance(vv, bytes) else vv
    return bytes(v)


def uncomp(b, i):
    x = b[i]
    if x & 0x80 == 0:
        return x, i + 1
    if x & 0xc0 == 0x80:
        return ((x & 0x3f) << 8) | b[i + 1], i + 2
    return ((x & 0x1f) << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3], i + 4


class Gen:
    def __init__(self, paths):
        self.mods = [Mod(p) for p in paths]
        self.types = {}      # fullname -> (mod, typedef row, kind)
        self.generics = []   # list of idl strings for declare
        self.genset = set()
        for m in self.mods:
            for t in m.md.TypeDef:
                if s(t.TypeName) == '<Module>':
                    continue
                full = s(t.TypeNamespace) + '.' + s(t.TypeName)
                self.types[full] = (m, t, self.kind(m, t))

    def extends(self, m, t):
        e = t.Extends
        if e is None or e.row is None:
            return ''
        r = e.row
        return s(getattr(r, 'TypeNamespace', '')) + '.' + s(getattr(r, 'TypeName', ''))

    def kind(self, m, t):
        if t.Flags.tdInterface:
            return 'interface'
        e = self.extends(m, t)
        if e == 'System.Enum':
            return 'enum'
        if e == 'System.ValueType':
            return 'struct'
        if e == 'System.MulticastDelegate':
            return 'delegate'
        return 'runtimeclass'

    def typename_of(self, m, tag, row):
        tbl = [m.md.TypeDef, m.md.TypeRef, m.md.TypeSpec][tag]
        r = tbl[row - 1]
        if tag == 2:
            return None, raw(r.Signature)
        return s(r.TypeNamespace) + '.' + s(r.TypeName), None

    def kind_of_name(self, full):
        if full in self.types:
            return self.types[full][2]
        ext = EXT_KIND.get(full)
        if ext:
            return ext
        raise KeyError('unknown external type ' + full)

    def parse_type(self, m, b, i):
        et = b[i]; i += 1
        if et in PRIM:
            return PRIM[et], i
        if et == 0x01:
            return 'void', i
        if et in (0x11, 0x12):
            ci, i = uncomp(b, i)
            name, spec = self.typename_of(m, ci & 3, ci >> 2)
            if spec is not None:
                t, _ = self.parse_type(m, spec, 0)
                return t, i
            if name in WELL:
                return WELL[name], i
            if name == 'System.Object':
                return 'IInspectable*', i
            k = self.kind_of_name(name)
            return (name + '*') if k in ('interface', 'runtimeclass', 'delegate') else name, i
        if et == 0x15:
            et2 = b[i]; i += 1
            ci, i = uncomp(b, i)
            name, _ = self.typename_of(m, ci & 3, ci >> 2)
            n, i = uncomp(b, i)
            args = []
            for _ in range(n):
                a, i = self.parse_type(m, b, i)
                args.append(a)
            base = name.split('`')[0]
            inst = '%s<%s>' % (base, ', '.join(args))
            self.add_generic(base, args)
            return inst + '*', i
        if et == 0x1d:
            t, i = self.parse_type(m, b, i)
            return ('ARRAY', t), i
        if et == 0x10:
            t, i = self.parse_type(m, b, i)
            return ('BYREF', t), i
        if et == 0x20 or et == 0x1f:  # cmod
            _, i = uncomp(b, i)
            return self.parse_type(m, b, i)
        raise ValueError('sig elem %#x' % et)

    def add_generic(self, base, args):
        key = (base, tuple(args))
        if key in self.genset:
            return
        self.genset.add(key)
        a = ', '.join(args)
        short = base.split('.')[-1]
        # companions first so they are declared
        if short in ('IVector', 'IVectorView', 'IObservableVector'):
            self.add_generic('Windows.Foundation.Collections.IIterable', args)
            self.add_generic('Windows.Foundation.Collections.IIterator', args)
            if short == 'IVector':
                self.add_generic('Windows.Foundation.Collections.IVectorView', args)
        if short in ('IMap', 'IMapView', 'IObservableMap'):
            kv = 'Windows.Foundation.Collections.IKeyValuePair<%s>*' % a
            self.add_generic('Windows.Foundation.Collections.IKeyValuePair', args)
            self.add_generic('Windows.Foundation.Collections.IIterable', [kv])
            self.add_generic('Windows.Foundation.Collections.IIterator', [kv])
            if short == 'IMap':
                self.add_generic('Windows.Foundation.Collections.IMapView', args)
        if short == 'IIterable':
            self.add_generic('Windows.Foundation.Collections.IIterator', args)
        if short == 'IAsyncOperation':
            self.add_generic('Windows.Foundation.AsyncOperationCompletedHandler', args)
        if short == 'IAsyncOperationWithProgress':
            self.add_generic('Windows.Foundation.AsyncOperationWithProgressCompletedHandler', args)
            self.add_generic('Windows.Foundation.AsyncOperationProgressHandler', args)
        self.generics.append('%s<%s>' % (base, a))

    def guid_of(self, m, t, table='TypeDef'):
        for ca in m.md.CustomAttribute:
            p = ca.Parent
            if p.row is not t:
                continue
            ty = ca.Type.row
            cls = getattr(ty, 'Class', None)
            cn = s(getattr(cls.row, 'TypeName', '')) if cls is not None and cls.row is not None else ''
            if cn == 'GuidAttribute':
                v = raw(ca.Value)
                return str(uuid.UUID(bytes_le=v[2:18]))
        return None

    def attr_names(self, m, row):
        out = []
        for ca in m.md.CustomAttribute:
            if ca.Parent.row is not row:
                continue
            cls = getattr(ca.Type.row, 'Class', None)
            cn = s(getattr(cls.row, 'TypeName', '')) if cls is not None and cls.row is not None else ''
            out.append((cn, raw(ca.Value)))
        return out

    def params(self, m, meth):
        sig = raw(meth.Signature)
        i = 0
        cc = sig[i]; i += 1
        if cc & 0x10:
            _, i = uncomp(sig, i)
        n, i = uncomp(sig, i)
        ret, i = self.parse_type(m, sig, i)
        ptypes = []
        for _ in range(n):
            t, i = self.parse_type(m, sig, i)
            ptypes.append(t)
        pinfo = {}
        for pr in (meth.ParamList or []):
            p = pr.row
            pinfo[p.Sequence] = (s(p.Name), p.Flags)
        out = []
        for k, t in enumerate(ptypes, 1):
            name, flags = pinfo.get(k, ('p%d' % k, None))
            isout = bool(flags and flags.pdOut)
            out.append(self.emit_param(name, t, isout))
        if ret != 'void':
            out.append(self.emit_param('value', ret, True, retval=True))
        return ', '.join(x for x in out if x)

    def emit_param(self, name, t, isout, retval=False):
        name = '_' + name
        if isinstance(t, tuple) and t[0] == 'BYREF':
            t = t[1]
            if isinstance(t, tuple) and t[0] == 'ARRAY':  # receive array
                el = t[1]
                return '[out] UINT32 *%s_size, [out, size_is(, *%s_size)] %s **%s' % (name, name, el, name)
            return '[out%s] %s *%s' % (', retval' if retval else '', t, name)
        if isinstance(t, tuple) and t[0] == 'ARRAY':
            el = t[1]
            if retval:
                return '[out] UINT32 *%s_size, [out, retval, size_is(, *%s_size)] %s **%s' % (name, name, el, name)
            if isout:  # fill array
                return '[in] UINT32 %s_size, [out, size_is(%s_size)] %s *%s' % (name, name, el, name)
            return '[in] UINT32 %s_size, [in, size_is(%s_size)] %s *%s' % (name, name, el, name)
        if retval:
            return '[out, retval] %s *%s' % (t, name)
        return '[in] %s %s' % (t, name)

    def iface_body(self, m, t):
        lines = []
        names = {}
        for mr in t.MethodList:
            meth = mr.row
            nm = s(meth.Name)
            if nm in ('.ctor',):
                continue
            for cn, v in self.attr_names(m, meth):
                if cn == 'OverloadAttribute':
                    ln, j = uncomp(v, 2)
                    nm = v[j:j + ln].decode()
            if nm in names:
                names[nm] += 1
                nm = '%s%d' % (nm, names[nm])
            else:
                names[nm] = 1
            lines.append('        HRESULT %s(%s);' % (nm, self.params(m, meth)))
        return lines

    def impls(self, m, t):
        out = []
        for ii in (m.md.InterfaceImpl or []):
            if ii.Class.row is not t:
                continue
            r = ii.Interface.row
            tag = ii.Interface.table.name if hasattr(ii.Interface, 'table') else ''
            if hasattr(r, 'TypeName'):
                nm = s(r.TypeNamespace) + '.' + s(r.TypeName)
            else:
                nm, _ = self.parse_type(m, raw(r.Signature), 0)
                nm = nm.rstrip('*')
            dflt = any(cn == 'DefaultAttribute' for cn, _ in self.attr_names(m, ii))
            out.append((nm, dflt))
        return out

    def run(self, out):
        bodies = {}
        for full, (m, t, k) in self.types.items():
            if k in ('interface', 'delegate'):
                bodies[full] = self.iface_body(m, t) if k == 'interface' else None
                if k == 'delegate':
                    inv = [mr.row for mr in t.MethodList if s(mr.row.Name) == 'Invoke'][0]
                    bodies[full] = self.params(m, inv)
            elif k == 'runtimeclass':
                bodies[full] = self.impls(m, t)
            if k == 'interface':
                bodies[full] = (bodies[full], self.impls(m, t))
        byns = {}
        for full, (m, t, k) in self.types.items():
            byns.setdefault(s(t.TypeNamespace), []).append((full, m, t, k))
        L = ['#ifdef __WIDL__', '#pragma winrt ns_prefix', '#endif',
             'import "inspectable.idl";', 'import "asyncinfo.idl";', 'import "eventtoken.idl";',
             'import "windowscontracts.idl";', 'import "windows.foundation.idl";',
             'import "windows.storage.streams.idl";', 'import "windows.data.json.idl";',
             'import "windows.system.idl";', 'import "windows.management.deployment.idl";', '',
             'namespace Windows.ApplicationModel {',
             '    typedef enum StartupTaskState StartupTaskState;',
             '    enum StartupTaskState { Disabled = 0, DisabledByUser = 1, Enabled = 2, DisabledByPolicy = 3, EnabledByPolicy = 4 };',
             '}', '']
        # pass 1: forward decls + enums + structs
        for ns, items in byns.items():
            L.append('namespace %s {' % ns)
            for full, m, t, k in items:
                n = s(t.TypeName)
                if k in ('interface', 'runtimeclass', 'delegate'):
                    L.append('    %s %s;' % ('interface' if k == 'interface' else k, n))
                elif k == 'enum':
                    L.append('    typedef enum %s %s;' % (n, n))
                elif k == 'struct':
                    L.append('    typedef struct %s %s;' % (n, n))
            for full, m, t, k in items:
                n = s(t.TypeName)
                if k == 'enum':
                    vals = []
                    for fr in t.FieldList:
                        f = fr.row
                        if s(f.Name) == 'value__':
                            continue
                        v = None
                        for c in m.md.Constant:
                            if c.Parent.row is f:
                                v = struct.unpack('<i', raw(c.Value)[:4])[0]
                        vals.append('        %s = %d' % (s(f.Name), v))
                    L.append('    enum %s {\n%s\n    };' % (n, ',\n'.join(vals)))
                elif k == 'struct':
                    fl = []
                    for fr in t.FieldList:
                        f = fr.row
                        ft, _ = self.parse_type(m, raw(f.Signature), 1)
                        fl.append('        %s %s;' % (ft, s(f.Name)))
                    L.append('    struct %s {\n%s\n    };' % (n, '\n'.join(fl)))
            L.append('}')
        L.append('@@DECLARE@@')
        # pass 2: delegates + interfaces
        for ns, items in byns.items():
            L.append('namespace %s {' % ns)
            for full, m, t, k in items:
                n = s(t.TypeName)
                if k == 'delegate':
                    L.append('    [uuid(%s)]\n    delegate HRESULT %s(%s);' % (self.guid_of(m, t), n, bodies[full]))
                if k == 'interface':
                    meths, req = bodies[full]
                    r = (' requires %s' % ', '.join(x for x, _ in req)) if req else ''
                    L.append('    [uuid(%s)]\n    interface %s : IInspectable%s\n    {\n%s\n    }' %
                             (self.guid_of(m, t), n, r, '\n'.join(meths)))
            L.append('}')
        # pass 3: runtimeclasses
        for ns, items in byns.items():
            L.append('namespace %s {' % ns)
            for full, m, t, k in items:
                if k != 'runtimeclass':
                    continue
                impl = bodies[full]
                if not impl:
                    L.append('    [contract(Windows.Foundation.FoundationContract, 1.0), marshaling_behavior(agile)]\n    runtimeclass %s\n    {\n    }' % s(t.TypeName))
                    continue
                if not any(d for _, d in impl):
                    impl[0] = (impl[0][0], True)
                il = ['        %sinterface %s;' % ('[default] ' if d else '', x) for x, d in impl]
                L.append('    [contract(Windows.Foundation.FoundationContract, 1.0), marshaling_behavior(agile)]\n    runtimeclass %s\n    {\n%s\n    }' % (s(t.TypeName), '\n'.join(il)))
            L.append('}')
        D = ['namespace XboxPcAppFT {\n    declare {']
        for g in self.generics:
            kind = 'interface'
            D.append('        %s %s;' % (kind, g))
        D.append('    }\n}')
        L[L.index('@@DECLARE@@')] = '\n'.join(D)
        open(out, 'w', newline='\n').write('\n'.join(L) + '\n')


EXT_KIND = {
    'Windows.Foundation.IAsyncAction': 'interface',
    'Windows.Foundation.EventRegistrationToken': 'struct',
    'Windows.Foundation.DateTime': 'struct',
    'Windows.Foundation.TimeSpan': 'struct',
    'Windows.Storage.Streams.IBuffer': 'interface',
    'Windows.System.DispatcherQueue': 'runtimeclass',
    'Windows.Data.Json.JsonObject': 'runtimeclass',
    'Windows.Management.Deployment.DeploymentResult': 'runtimeclass',
    'Windows.Management.Deployment.DeploymentProgress': 'struct',
    'Windows.Foundation.Uri': 'runtimeclass',
    'Windows.ApplicationModel.StartupTaskState': 'enum',
}

if __name__ == '__main__':
    Gen(sys.argv[2:]).run(sys.argv[1])
