class Program {
    static void Main() {
        List<int> xs = new List<int>();
        xs.Add(4);
        xs.Add(5);
        xs.Add(6);

        int last = xs.LastOrDefault();
        int mid = xs.ElementAtOrDefault(1);
        int miss = xs.ElementAtOrDefault(99);

        int[] arr = xs.ToArray();
        bool same = xs.SequenceEqual(arr);

        Console.WriteLine(last + mid + miss + (same ? 1 : 0));
    }
}
