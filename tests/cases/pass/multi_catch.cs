class Program {
    static void Main() {
        try {
            throw 7;
        } catch (string s) {
            Console.WriteLine("S");
        } catch (int n) {
            Console.WriteLine("N=" + n);
        } finally {
            Console.WriteLine("F");
        }
    }
}
