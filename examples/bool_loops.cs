class Program {
    static void Main() {
        bool enabled = true;
        int i = 0;

        while (enabled) {
            i = i + 1;
            if (i == 2) {
                continue;
            }
            if (i == 4) {
                break;
            }
            Console.WriteLine(i);
        }

        Console.WriteLine(enabled);
    }
}
