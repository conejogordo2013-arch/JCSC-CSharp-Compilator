class Program {
    static void Main() {
        int[] a = new int[3];
        a[0] = 5;
        a[1] = 7;
        a[2] = a[0] + a[1];
        Console.WriteLine(a[2]);
    }
}
