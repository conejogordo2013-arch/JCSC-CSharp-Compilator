class Program
{
    static int Add(int a, int b)
    {
        return a + b;
    }

    static void Main()
    {
        int total = 0;

        for (int i = 0; i < 5; i = i + 1)
        {
            total = Add(total, i);
        }

        if (total > 5)
        {
            System.Console.WriteLine("Total > 5");
        }
        else
        {
            System.Console.WriteLine("Total <= 5");
        }

        while (total > 0)
        {
            total = total - 1;
        }

        System.Console.WriteLine("Done");
    }
}
