class A {}
class B {}

class Program {
    static void Show(A a) {
        Console.WriteLine(10);
    }

    static void Show(B b) {
        Console.WriteLine(20);
    }

    static void Main() {
        Show(new A());
        Show(new B());
    }
}
