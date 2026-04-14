class Program {
    static void Main() {
        try {
            throw "all";
        } catch {
            Console.WriteLine("catched-all");
        }
    }
}
