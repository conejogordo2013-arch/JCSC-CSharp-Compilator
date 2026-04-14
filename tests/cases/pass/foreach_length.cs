class Program {
    static void Main() {
        int[] xs = new int[4];
        xs[0] = 3;
        xs[1] = 4;
        xs[2] = 5;
        xs[3] = 6;

        int sum = 0;
        foreach (int x in xs) {
            sum = sum + x;
        }

        Console.WriteLine(xs.Length);
        Console.WriteLine(sum);
    }
}
