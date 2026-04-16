class Program {
    static void Main() {
        List<int> xs = new List<int>();
        xs.Add(1);
        xs.Add(3);
        xs.Insert(1, 2);
        int[] arr = xs.ToArray();
        Console.WriteLine(arr[0] + arr[1] + arr[2]);
    }
}
