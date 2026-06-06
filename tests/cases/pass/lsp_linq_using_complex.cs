using static System.Console;
using LinqAlias = System.Linq;

interface IAnimal {
    int Speak();
}

class Animal : IAnimal {
    int Speak() { return 1; }
}

class Dog : Animal {
    int Speak() { return 2; }
}

class Program {
    static IAnimal Build() {
        Animal a = new Dog();
        return a;
    }

    static void Main() {
        List<int> nums = new List<int>();
        nums.Add(1);
        nums.Add(4);
        nums.Add(8);

        List<int> filtered = nums.Where(4);
        List<int> projected = filtered.Select(1);
        int total = projected.Sum();
        int first = projected.FirstOrDefault();

        int sumForeach = 0;
        foreach (int x in projected) {
            sumForeach += x;
        }

        IAnimal pet = Build();
        Console.WriteLine(pet.Speak() + total + first + sumForeach);
    }
}
