class Program {
    static int Sum(int a, int b) {
        return a + b;
    }

    static int Sum(int a, int b, int c) {
        return a + b + c;
    }

    static void Main() {
        Console.WriteLine(Sum(2, 3));
        Console.WriteLine(Sum(2, 3, 4));
    }
}
