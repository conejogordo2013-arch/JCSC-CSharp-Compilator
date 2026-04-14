class Program {
    static void Main() {
        try {
            throw "boom";
        } catch (string e) {
            Console.WriteLine("capturado: " + e);
        }
    }
}
