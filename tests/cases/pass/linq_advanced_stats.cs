class Program {
    static void Main() {
        List<int> rev = new List<int>();
        rev.Add(3);
        rev.Add(9);
        rev.Add(1);
        rev.Add(3);
        rev.Add(7);

        rev = rev.Distinct().OrderBy().Reverse();
        int minv = rev.Min();
        int maxv = rev.Max();
        int avg = rev.Average();
        bool has7 = rev.Contains(7);

        Console.WriteLine(minv + maxv + avg + (has7 ? 1 : 0));
    }
}
