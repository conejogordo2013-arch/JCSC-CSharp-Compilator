class Program {
    static void Main() {
        int x = 1;
        {
            int x = 2;
            Console.WriteLine(x);
        }
        Console.WriteLine(x);
    }
}
