class Box<T> {
    T value;

    static int GenericMethod<U>() {
        return 9;
    }

    int Get() {
        return 5;
    }
}

class Program {
    static void Main() {
        Box<int> b = new Box<int>();
        Console.WriteLine(b.Get());
    }
}
