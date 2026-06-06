class Program {
    static async int Work() {
        return 7;
    }

    static void Main() {
        int x = await Work();
        Console.WriteLine(x);
    }
}
