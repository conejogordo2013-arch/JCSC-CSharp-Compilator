class Program {
    static void Main() {
        List<int> xs = new List<int>();
        xs.Add(1);
        List<int> ys = xs.Where(true);
        Console.WriteLine(ys.Count);
    }
}
