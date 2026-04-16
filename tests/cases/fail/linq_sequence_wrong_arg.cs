class Program {
    static void Main() {
        List<int> xs = new List<int>();
        xs.Add(1);
        int a = xs.ElementAtOrDefault(true);
        Console.WriteLine(a);
    }
}
