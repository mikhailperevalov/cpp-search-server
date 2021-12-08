// Решите загадку: Сколько чисел от 1 до 1000 содержат как минимум одну цифру 3?
// Напишите ответ здесь:

#include <iostream>

int main()
{
    int count = 0;
    for (int i = 1; i < 1000; ++i) {
        int n1, n2, n3;
        n1 = i / 100;
        n2 = (i % 100) / 10;
        n3 = i % 10;
        if (n1 == 3 || n2 == 3 || n3 == 3)
            ++count;
    }
    std::cout << count << std::endl;
    system("pause");
    return 0;
}
// Закомитьте изменения и отправьте их в свой репозиторий.
