interface IGreeter {
    void JC();
}

class Greeter : IGreeter {
    public void JC() {
        Console.WriteLine("JuanCarlos");
    }
}

class Program {
    static void Main() {
        Greeter g = new Greeter();
        g.JC();
    }
}
