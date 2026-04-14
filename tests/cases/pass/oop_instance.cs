class Counter {
    int value;
    void Inc() { value = value + 1; }
    int Get() { return value; }
}

class Program {
    static void Main() {
        Counter c = new Counter();
        c.Inc();
        c.Inc();
        Console.WriteLine(c.Get());
    }
}
