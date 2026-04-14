public class Program {
    static int Sum(int a, int b) {
        return a + b;
    }

    static void Main() {
        int result = Sum(4, 5);
        if (result > 5) {
            Console.WriteLine("ok");
        }
    }
}
