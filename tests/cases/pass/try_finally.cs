class Program {
    static void Main() {
        try {
            Console.WriteLine("A");
        } catch (string e) {
            Console.WriteLine("B");
        } finally {
            Console.WriteLine("F");
        }
    }
}
