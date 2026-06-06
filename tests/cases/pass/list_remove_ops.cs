class Program {
    static void Main() {
        List<int> xs = new List<int>();
        xs.Add(1);
        xs.Add(2);
        xs.Add(3);
        bool removed = xs.Remove(2);
        xs.RemoveAt(0);
        Console.WriteLine((removed ? 1 : 0) + xs.Count + xs[0]);
    }
}
