using System.Collections.Generic;

class Program {
    static void Main() {
        List<int> xs = new List<int>();
        xs.Add(10);
        xs.Add(20);
        Console.WriteLine(xs.Count);
        Console.WriteLine(xs[0] + xs[1]);
        xs[1] = 7;
        Console.WriteLine(xs[1]);
        xs.Clear();
        Console.WriteLine(xs.Count);
    }
}
