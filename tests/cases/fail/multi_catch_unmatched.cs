class Program {
    static void Main() {
        try {
            throw 9;
        } catch (string s) {
            Console.WriteLine(s);
        }
    }
}
