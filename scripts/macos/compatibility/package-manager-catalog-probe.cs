using System;
using System.Collections;
using System.Reflection;
using System.Runtime.InteropServices;

public static class PackageManagerCatalogProbe
{
    public static int Main()
    {
        try
        {
            Type type = Type.GetType("Windows.Management.Deployment.PackageManager, Windows.Management, ContentType=WindowsRuntime", true);
            object manager = Activator.CreateInstance(type);
            MethodInfo method = type.GetMethod("FindPackagesForUser", new[] { typeof(string), typeof(string) })
                ?? type.GetMethod("FindPackagesByUserSecurityIdPackageFamilyName", new[] { typeof(string), typeof(string) });
            if (method == null) throw new MissingMethodException("No genuine user/family package query");
            Console.WriteLine("METADATA_METHOD=" + method);
            foreach (string family in new[] {
                "Microsoft.GamingApp_8wekyb3d8bbwe",
                "Microsoft.GamingServices_8wekyb3d8bbwe",
                "Unregistered.Package_123456789abcd"
            })
            {
                object packages = method.Invoke(manager, new object[] { "", family });
                int count = 0;
                foreach (object package in (IEnumerable)packages) ++count;
                if (count != 0) throw new Exception("Fresh inventory unexpectedly contains registrations");
                Console.WriteLine("ACTUAL_FAMILY_QUERY_COUNT=" + count);
                Marshal.ReleaseComObject(packages);
            }
            Marshal.ReleaseComObject(manager);
            Console.WriteLine("PACKAGE_MANAGER_CATALOG_METADATA_PROBE_PASS");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
