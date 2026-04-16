using static System.Console;
using Alias = System.Linq;

namespace Demo.Showcase {
interface IEntity {
    int Id();
    string Kind();
}

class EntityBase : IEntity {
    int id;
    string kind;

    int Id() { return id; }
    string Kind() { return kind ?? "unknown"; }
}

class PowerEntity : EntityBase {
    int power;

    int Score() {
        return Id() + power;
    }
}

class Program {
    static IEntity Build() {
        PowerEntity e = new PowerEntity();
        e.id = 10;
        e.kind = "power";
        e.power = 7;
        return e;
    }

    static int Analyze(List<int> nums) {
        List<int> pipeline = nums
            .Where(4)
            .Select(2)
            .Distinct()
            .OrderBy()
            .Reverse()
            .Take(3);

        int[] arr = pipeline.ToArray();
        bool eq = pipeline.SequenceEqual(arr);
        int sum = pipeline.Sum();
        int minv = pipeline.Min();
        int maxv = pipeline.Max();
        int avg = pipeline.Average();
        int last = pipeline.LastOrDefault();
        int at1 = pipeline.ElementAtOrDefault(1);

        int foreachSum = 0;
        foreach (int x in pipeline) {
            foreachSum += x;
        }

        if (!eq) return -1;
        return sum + minv + maxv + avg + last + at1 + foreachSum;
    }

    static void Main() {
        List<int> nums = new List<int>();
        nums.Add(1);
        nums.Add(4);
        nums.Add(9);
        nums.Add(4);
        nums.Add(7);
        nums.Insert(1, 5);
        nums.Remove(1);

        IEntity ent = Build();
        int report = Analyze(nums);

        try {
            if (report > 0) {
                throw "ok";
            }
        } catch (string ex) {
            Console.WriteLine("capturado:" + ex);
        } finally {
            Console.WriteLine("finally");
        }

        Console.WriteLine(ent.Kind());
        Console.WriteLine(ent.Id());
        Console.WriteLine(report);
    }
}
}
