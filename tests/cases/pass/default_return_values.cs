class Obj {}

class Program {
    static bool B() {
    }

    static string S() {
    }

    static Obj O() {
    }

    static void Main() {
        Console.WriteLine(B());
        Console.WriteLine(S());
        Console.WriteLine(O() == null);
    }
}
