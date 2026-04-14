class Program {
    static bool Boom() {
        while (true) {
        }
        return true;
    }

    static void Main() {
        if (false && Boom()) {
            Console.WriteLine(111);
        }
        Console.WriteLine(1);
        if (true || Boom()) {
            Console.WriteLine(222);
        }
        Console.WriteLine(2);
    }
}
