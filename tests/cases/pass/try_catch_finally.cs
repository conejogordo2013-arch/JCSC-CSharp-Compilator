class Program {
    static void Main() {
        try {
            throw "x";
        } catch (string e) {
            Console.WriteLine("C:" + e);
        } finally {
            Console.WriteLine("F");
        }
    }
}
