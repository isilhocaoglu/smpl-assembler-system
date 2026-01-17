#ifndef LOADER_H
#define LOADER_H

#include "tables.h"

//Loader ana fonksiyonu
//.exe ve .t (DAT) okuyup M[]'ye yükler
void runLoader(const char *exe_file, const char *t_file);

//Memory array'i ekrana yazdırır
void printMemoryArray(int start_addr, int end_addr);

//Adresi iki parçaya böler: 1033 -> "10" "33"
void intToTwoCharString(int value, char *high, char *low);

#endif //LOADER_H

