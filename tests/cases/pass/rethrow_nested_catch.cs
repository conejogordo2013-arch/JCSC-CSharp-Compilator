class Program {
    static void Main() {
        try {
            try {
                throw "inner";
            } catch (string e) {
                Console.WriteLine("mid");
                throw;
            }
        } catch (string e2) {
            Console.WriteLine("outer:" + e2);
        }
    }
}
