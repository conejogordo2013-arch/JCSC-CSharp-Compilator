class Box { public int value; }

class Program {
    static void Main() {
        Box b = null;
        Console.WriteLine(b == null);
        b = new Box();
        Console.WriteLine(b != null);

        int[] xs = null;
        Console.WriteLine(xs == null);
        xs = new int[2];
        Console.WriteLine(xs != null);
        Console.WriteLine(xs.Length);
    }
}
