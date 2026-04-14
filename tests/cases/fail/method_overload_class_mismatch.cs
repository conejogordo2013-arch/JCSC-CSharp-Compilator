class A {}
class B {}

class Program {
    static void Show(A a) {
        Console.WriteLine(10);
    }

    static void Main() {
        Show(new B());
    }
}
