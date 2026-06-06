class Program {
    static void Main() {
        string a = null;
        string b = "Juan";
        Console.WriteLine(a ?? "Carlos");
        Console.WriteLine(b ?? "Carlos");
    }
}
