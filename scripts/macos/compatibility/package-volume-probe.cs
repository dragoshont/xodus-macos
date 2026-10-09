using System;
using System.Reflection;
using System.Runtime.InteropServices;

public static class PackageVolumeProbe
{
    public static int Main()
    {
        try
        {
            Type type = Type.GetType("Windows.Management.Deployment.PackageManager, Windows.Management, ContentType=WindowsRuntime", true);
            object manager = Activator.CreateInstance(type);
            object volume = type.GetMethod("GetDefaultPackageVolume", Type.EmptyTypes).Invoke(manager, null);
            if (volume == null) throw new Exception("Default volume is null");
            Console.WriteLine("DEFAULT_VOLUME=" + volume.GetType().FullName);
            foreach (string property in new[] {
                "MountPoint", "Name", "IsOffline", "IsSystemVolume", "SupportsHardLinks",
                "IsFullTrustPackageSupported", "IsAppxInstallSupported"
            })
                Console.WriteLine(property + "=" + volume.GetType().GetProperty(property).GetValue(volume, null));
            try
            {
                volume.GetType().GetProperty("PackageStorePath").GetValue(volume, null);
                throw new Exception("Unimplemented store reported success");
            }
            catch (TargetInvocationException error)
            {
                if (error.InnerException.HResult != unchecked((int)0x80070032)) throw;
                Console.WriteLine("PackageStorePath=ERROR_NOT_SUPPORTED (no durable deployment backend)");
            }
            object operation = volume.GetType().GetMethod("GetAvailableSpaceAsync").Invoke(volume, null);
            // The projected generic result type comes from the genuine Windows metadata.
            MethodInfo resultMethod = operation.GetType().GetMethod("GetResults");
            if (resultMethod == null)
            {
                Type generic = Type.GetType("Windows.Foundation.IAsyncOperation`1, Windows, ContentType=WindowsRuntime", true);
                resultMethod = generic.MakeGenericType(typeof(ulong)).GetMethod("GetResults");
            }
            ulong available = (ulong)resultMethod.Invoke(operation, null);
            if (available == 0) throw new Exception("No real free capacity");
            Console.WriteLine("AVAILABLE_BYTES=" + available);
            Marshal.ReleaseComObject(operation);
            Marshal.ReleaseComObject(volume);
            Marshal.ReleaseComObject(manager);
            Console.WriteLine("PACKAGE_VOLUME_METADATA_PROBE_PASS");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
