// Headless BlueBrick grid snap: for each case, place a brick, grab it and
// ask BlueBrick's LayerBrick.getMovedSnapPoint where the drag puts it, with
// the grid on and nothing to connect to. Test oracle only.
//
//   bbsnap.exe cases.txt
//
// Each line of cases.txt: <part> <orientation> <grid> <centre x> <centre y>
// <grab x> <grab y> <mouse x> <mouse y>. Prints, per case, the part's
// display area, its snap offset (from <SnapMargin>) and the centre the drag
// gives the brick.
using System;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Reflection;

class BbSnap {
    const BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;

    static object Get(object o, string name) {
        Type t = o as Type ?? o.GetType();
        object target = o is Type ? null : o;
        PropertyInfo p = t.GetProperty(name, Any);
        if (p != null) return p.GetValue(target, null);
        return t.GetField(name, Any).GetValue(target);
    }

    static void Set(object o, string name, object v) {
        Type t = o as Type ?? o.GetType();
        object target = o is Type ? null : o;
        t.GetProperty(name, Any).SetValue(target, v, null);
    }

    static float F(string s) { return float.Parse(s, CultureInfo.InvariantCulture); }
    static string S(float f) { return f.ToString("R", CultureInfo.InvariantCulture); }

    [STAThread]
    static int Main(string[] args) {
        try {
            Assembly bb = Assembly.LoadFrom(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "BlueBrick.exe"));
            Type settingsType = bb.GetType("BlueBrick.Properties.Settings");
            object settings = settingsType.GetProperty("Default", BindingFlags.Public | BindingFlags.Static).GetValue(null, null);
            PropertyInfo language = settingsType.GetProperty("Language");
            if (string.IsNullOrEmpty((string)language.GetValue(settings, null))) language.SetValue(settings, "en", null);
            Activator.CreateInstance(bb.GetType("BlueBrick.MainForm"), new object[] { null });

            Type layerType = bb.GetType("BlueBrick.MapData.Layer");
            Type layerBrickType = bb.GetType("BlueBrick.MapData.LayerBrick");
            Type brickType = bb.GetType("BlueBrick.MapData.LayerBrick+Brick");
            Type itemType = bb.GetType("BlueBrick.MapData.Layer+LayerItem");
            MethodInfo grab = layerBrickType.GetMethod("setBrickUnderMouse", Any, null, new Type[] { brickType, typeof(PointF) }, null);
            MethodInfo snap = layerBrickType.GetMethod("getMovedSnapPoint", Any, null, new Type[] { typeof(PointF), itemType }, null);

            foreach (string raw in File.ReadAllLines(args[0])) {
                string line = raw.Trim();
                if (line.Length == 0 || line.StartsWith("#")) continue;
                string[] a = line.Split(new char[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                Set(layerType, "SnapGridEnabled", true);
                Set(layerType, "CurrentSnapGridSize", F(a[2]));
                object layer = Activator.CreateInstance(layerBrickType, true);
                object brick = Activator.CreateInstance(brickType, new object[] { a[0] });
                Set(brick, "Orientation", F(a[1]));
                Set(brick, "Center", new PointF(F(a[3]), F(a[4])));
                RectangleF area = (RectangleF)Get(brick, "DisplayArea");
                PointF offset = (PointF)Get(brick, "SnapToGridOffset");
                grab.Invoke(layer, new object[] { brick, new PointF(F(a[5]), F(a[6])) });
                PointF centre = (PointF)snap.Invoke(layer, new object[] { new PointF(F(a[7]), F(a[8])), brick });
                Console.WriteLine(line + " => area " + S(area.X) + " " + S(area.Y) + " " + S(area.Width) + " " + S(area.Height)
                                  + " offset " + S(offset.X) + " " + S(offset.Y)
                                  + " centre " + S(centre.X) + " " + S(centre.Y));
            }
            return 0;
        } catch (Exception e) {
            Console.Error.WriteLine(e.ToString());
            return 1;
        }
    }
}
