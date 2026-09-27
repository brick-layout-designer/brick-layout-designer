// Headless BlueBrick flex move: load a map, select every brick of the
// grabbed brick's layer, flex-move the grabbed brick through a sequence of
// target points (as mouse moves, without connection snapping) and save.
// Test oracle only.
//
//   bbflex.exe in.bbm out.bbm <brick id> <grab x> <grab y> <x1> <y1> [<x2> <y2> ...]
using System;
using System.Collections;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Reflection;

class BbFlex {
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
            // fails reading its colour names. Use its shipped default.
            Type settingsType = bb.GetType("BlueBrick.Properties.Settings");
            object settings = settingsType.GetProperty("Default", BindingFlags.Public | BindingFlags.Static).GetValue(null, null);
            PropertyInfo language = settingsType.GetProperty("Language");
            if (string.IsNullOrEmpty((string)language.GetValue(settings, null))) language.SetValue(settings, "en", null);
            Activator.CreateInstance(bb.GetType("BlueBrick.MainForm"), new object[] { null });
            Type slm = bb.GetType("BlueBrick.SaveLoadManager");
            if (!(bool)slm.GetMethod("load", Any).Invoke(null, new object[] { args[0] })) return 2;

            object map = Get(bb.GetType("BlueBrick.MapData.Map"), "Instance");
            object layer = null, grabbed = null;
            foreach (object l in (IEnumerable)Get(map, "LayerList")) {
                if (l.GetType().Name != "LayerBrick") continue;
                foreach (object b in (IEnumerable)Get(l, "BrickList"))
                    if (Get(b, "GUID").ToString() == args[2]) { layer = l; grabbed = b; }
            }
            if (grabbed == null) { Console.Error.WriteLine("no brick " + args[2]); return 4; }

            Type itemType = bb.GetType("BlueBrick.MapData.Layer+LayerItem");
            IList selection = (IList)Activator.CreateInstance(typeof(System.Collections.Generic.List<>).MakeGenericType(itemType));
            foreach (object b in (IEnumerable)Get(layer, "BrickList")) selection.Add(b);

            Type flexType = bb.GetType("BlueBrick.Actions.Bricks.FlexMove");
            object flex = Activator.CreateInstance(flexType, Any, null,
                new object[] { layer, selection, grabbed, new PointF(F(args[3]), F(args[4])) }, null);
            if (!(bool)Get(flex, "IsValid")) { Console.Error.WriteLine("not a flex move"); return 5; }

            MethodInfo reach = flexType.GetMethod("reachTarget", Any);
            MethodInfo update = flexType.GetMethod("update", Any);
            for (int i = 5; i + 1 < args.Length; i += 2) {
                reach.Invoke(flex, new object[] { new PointF(F(args[i]), F(args[i + 1])), null });
                while ((bool)update.Invoke(flex, null)) { }
            }
            flexType.GetMethod("finishActionConstruction", Any).Invoke(flex, null);
            Type am = bb.GetType("BlueBrick.Actions.ActionManager");
            am.GetMethod("doAction", Any).Invoke(Get(am, "Instance"), new object[] { flex });

            if (!(bool)slm.GetMethod("save", Any).Invoke(null, new object[] { args[1] })) return 3;
            Console.WriteLine("flexed " + args[0] + " -> " + args[1]);
            return 0;
        } catch (Exception e) {
            Console.Error.WriteLine(e.ToString());
            return 1;
        }
    }
}
