// Headless BlueBrick rotate: load a map, then for each (<ids|all>, <steps>)
// pair select those bricks and run RotateBrick by that many rotation steps
// (90 degrees; negative is clockwise), as consecutive actions, and save.
// Test oracle only.
//
//   bbrotate.exe in.bbm out.bbm [--step <degrees>] <id,id,...|all> <steps> [<ids> <steps> ...]
using System;
using System.Collections;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Reflection;

class BbRotate {
    const BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;

    static object Get(object o, string name) {
        Type t = o as Type ?? o.GetType();
        object target = o is Type ? null : o;
        PropertyInfo p = t.GetProperty(name, Any);
        if (p != null) return p.GetValue(target, null);
        return t.GetField(name, Any).GetValue(target);
    }

    static float F(string s) { return float.Parse(s, CultureInfo.InvariantCulture); }

    [STAThread]
    static int Main(string[] args) {
        try {
            Assembly bb = Assembly.LoadFrom(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "BlueBrick.exe"));
            // Under Mono the settings can come back empty; BlueBrick then
            // fails reading its color names. Use its shipped default.
            Type settingsType = bb.GetType("BlueBrick.Properties.Settings");
            object settings = settingsType.GetProperty("Default", BindingFlags.Public | BindingFlags.Static).GetValue(null, null);
            PropertyInfo language = settingsType.GetProperty("Language");
            if (string.IsNullOrEmpty((string)language.GetValue(settings, null))) language.SetValue(settings, "en", null);
            Activator.CreateInstance(bb.GetType("BlueBrick.MainForm"), new object[] { null });
            Type slm = bb.GetType("BlueBrick.SaveLoadManager");
            if (!(bool)slm.GetMethod("load", Any).Invoke(null, new object[] { args[0] })) return 2;

            object map = Get(bb.GetType("BlueBrick.MapData.Map"), "Instance");
            Type itemType = bb.GetType("BlueBrick.MapData.Layer+LayerItem");
            Type rotType = bb.GetType("BlueBrick.Actions.Bricks.RotateBrick");
            Type am = bb.GetType("BlueBrick.Actions.ActionManager");
            int first = 2;
            if (args.Length > 3 && args[2] == "--step") {
                bb.GetType("BlueBrick.MapData.Layer").GetProperty("CurrentRotationStep", Any).SetValue(null, F(args[3]), null);
                first = 4;
            }
            for (int i = first; i + 1 < args.Length; i += 2) {
                object layer = null;
                IList selection = (IList)Activator.CreateInstance(typeof(System.Collections.Generic.List<>).MakeGenericType(itemType));
                string[] ids = args[i].Split(',');
                foreach (object l in (IEnumerable)Get(map, "LayerList")) {
                    if (l.GetType().Name != "LayerBrick") continue;
                    foreach (object b in (IEnumerable)Get(l, "BrickList"))
                        if (args[i] == "all" || Array.IndexOf(ids, Get(b, "GUID").ToString()) >= 0) { layer = l; selection.Add(b); }
                }
                if (layer == null) { Console.Error.WriteLine("no bricks " + args[i]); return 4; }
                object rot = Activator.CreateInstance(rotType, Any, null,
                    new object[] { layer, selection, int.Parse(args[i + 1]) }, null);
                am.GetMethod("doAction", Any).Invoke(Get(am, "Instance"), new object[] { rot });
            }
            if (!(bool)slm.GetMethod("save", Any).Invoke(null, new object[] { args[1] })) return 3;
            Console.WriteLine("rotated " + args[0] + " -> " + args[1]);
            return 0;
        } catch (Exception e) {
            Console.Error.WriteLine(e.ToString());
            return 1;
        }
    }
}
