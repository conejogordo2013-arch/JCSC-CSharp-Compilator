struct Counter {
    int value;

    int Next() {
        value++;
        return value;
    }
}

class Program {
    static void Main() {
        Counter c = new Counter();
        Console.WriteLine(c.Next());
        Console.WriteLine(c.Next());
    }
}
