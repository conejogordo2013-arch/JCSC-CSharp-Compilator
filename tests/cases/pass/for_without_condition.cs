class Program {
    static void Main() {
        int i = 0;
        for (; ; i = i + 1) {
            if (i == 3) break;
            Console.WriteLine(i);
        }
    }
}
