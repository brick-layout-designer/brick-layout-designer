// Headless BlueBrick: load a map with BlueBrick's own SaveLoadManager and
// save it in another format (chosen by extension). Test oracle only.
using System;
using System.IO;
using System.Reflection;
using System.Windows.Forms;

class BbConv {
    [STAThread]
    static int Main(string[] args) {
        try {
            Assembly bb = Assembly.LoadFrom(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "BlueBrick.exe"));
            Type mainForm = bb.GetType("BlueBrick.MainForm");
            // Loads the part library from <exe dir>/parts and sets MainForm.Instance.
            Activator.CreateInstance(mainForm, new object[] { null });
            Type slm = bb.GetType("BlueBrick.SaveLoadManager");
            MethodInfo load = slm.GetMethod("load", BindingFlags.Public | BindingFlags.Static);
            MethodInfo save = slm.GetMethod("save", BindingFlags.Public | BindingFlags.Static);
            for (int i = 0; i + 1 < args.Length; i += 2) {
                bool ok = (bool)load.Invoke(null, new object[] { args[i] });
                if (!ok) { Console.Error.WriteLine("load failed: " + args[i]); return 2; }
                ok = (bool)save.Invoke(null, new object[] { args[i + 1] });
                if (!ok) { Console.Error.WriteLine("save failed: " + args[i + 1]); return 3; }
                Console.WriteLine("converted " + args[i] + " -> " + args[i + 1]);
            }
            return 0;
        } catch (Exception e) {
            Console.Error.WriteLine(e.ToString());
            return 1;
        }
    }
}
