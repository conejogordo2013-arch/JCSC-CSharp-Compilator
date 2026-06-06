global using static System.Console;
using Data = System.Linq;

interface IEntity { int Id(); }
class BaseEntity : IEntity {
    int id;
    int Id() { return id; }
}
class User : BaseEntity {
    int Score() { return id + 10; }
}

class Program {
    static IEntity Build(int id) {
        User u = new User();
        u.id = id;
        return u;
    }

    static void Main() {
        int[] raw = new int[6];
        raw[0] = 9;
        raw[1] = 2;
        raw[2] = 9;
        raw[3] = 7;
        raw[4] = 1;
        raw[5] = 7;

        List<int> q = raw.Where(2).Distinct().OrderBy().Skip(1).Take(2).Select(1);
        int total = q.Sum();
        int cnt = q.Count();
        bool any = q.Any();
        bool all = q.All(8);

        IEntity e = Build(5);
        Console.WriteLine(e.Id() + total + cnt + (any ? 1 : 0) + (all ? 1 : 0));
    }
}
