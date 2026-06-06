class Program {
    static void Show(int x) {
        Console.WriteLine(x);
    }

    static void Show(bool x) {
        if (x) Console.WriteLine(1);
        else Console.WriteLine(0);
    }

    static void Main() {
        Show("hola");
    }
}
