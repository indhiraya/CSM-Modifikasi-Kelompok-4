#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <dirent.h>

/* semua keluaran program lewat OUT (layar atau file csmBaru.txt) */
static FILE * OUT;
#define printf(...) fprintf(OUT, __VA_ARGS__)

/*
 * CSM (Cost Supply Method) dengan modifikasi "SLC BERANTAI" (Chain-SLC).
 *
 * TIDAK ada look-ahead / nested CSM, TIDAK ada MODI. Kerangka CSM tetap:
 *   TC, TCS, FLC per kolom (alokasi awal), status ER / NR / Satisfied,
 *   pilih Diff terkecil, pindahkan unit dari FLC ke SLC, ulangi sampai semua baris Satisfied.
 *
 * Yang dimodifikasi hanya cara menghitung Diff (biaya relokasi satu unit):
 *
 *   CSM asli : Diff = c[SLC] - c[FLC]  -> unit hanya boleh pindah LANGSUNG ke baris NR.
 *
 *   Modifikasi: unit dari sel (ER) boleh juga pindah lewat SATU baris perantara
 *               (baris yang sudah Satisfied), yang lalu mengembalikan unitnya ke baris NR
 *               pada kolom lain:
 *
 *        Diff langsung = c[k][j]  - c[i][j]                         (i = ER, k = NR)
 *        Diff berantai = (c[m][j] - c[i][j]) + (c[k][j2] - c[m][j2]) (m = Satisfied, k = NR, j2 != j)
 *
 *   Diff sel = min(Diff langsung, Diff berantai). Sel dengan Diff terkecil dipilih
 *   (seri: baris tujuan dengan TC lebih besar, lalu rute lebih pendek, lalu baris ER lebih kecil).
 *   Jumlah unit = min(kelebihan baris ER, kebutuhan baris NR, isi tiap sel pada rute).
 *
 *   MAXHOP = 1 -> sama seperti CSM asli (hanya langsung)
 *   MAXHOP = 2 -> ada 1 baris perantara (disarankan, mudah dihitung manual)
 *   MAXHOP = 3 -> ada 2 baris perantara (hampir selalu optimal, hitung manual lebih panjang)
 */
#ifndef MAXHOP
#define MAXHOP 2
#endif

char inputName[100];
int row, col, matrix[100][100], s[100];
int TC[100] = {}, CS[100] = {}, supply[100][100], sumRow[100] = {};
int status[100];
int demand[100];
int demand_index = 0;

typedef struct {
  int ok, cost, len, src, dst, amt;
  int from[4], to[4], cl[4];
} Route;

void minimumCell(FILE * inFile) {
  int i, j, minimum;
  for (j = 0; j < col; j++) {
    minimum = 0;
    for (i = 1; i < row; i++) {
      if (matrix[minimum][j] > matrix[i][j]) minimum = i;
      else if (matrix[minimum][j] == matrix[i][j]) {
        if (TC[i] > TC[minimum]) minimum = i;
      }
    }
    fscanf(inFile, "%d", & supply[minimum][j]);
    demand[demand_index++] = supply[minimum][j];
  }
  for (i = 0; i < row; i++)
    for (j = 0; j < col; j++) sumRow[i] += supply[i][j];
}

// 1 = Satisfied, 2 = Need Supply, 3 = Excess Row
void updateStatus() {
  int i;
  for (i = 0; i < row; i++) {
    if (sumRow[i] == s[i]) status[i] = 1;
    else if (sumRow[i] < s[i]) status[i] = 2;
    else status[i] = 3;
  }
}

int allSatisfied() {
  int i;
  for (i = 0; i < row; i++)
    if (sumRow[i] != s[i]) return 0;
  return 1;
}

int totalCost() {
  int i, j, t = 0;
  for (i = 0; i < row; i++)
    for (j = 0; j < col; j++) t += matrix[i][j] * supply[i][j];
  return t;
}

/* a lebih baik dari b ? (biaya kecil, TC tujuan besar, rute pendek, baris sumber kecil) */
int better(Route * a, Route * b) {
  if (!b -> ok) return 1;
  if (a -> cost != b -> cost) return a -> cost < b -> cost;
  if (TC[a -> dst] != TC[b -> dst]) return TC[a -> dst] > TC[b -> dst];
  if (a -> len != b -> len) return a -> len < b -> len;
  return a -> src < b -> src;
}

static Route cur;
static Route * target; /* tempat menyimpan rute terbaik dari pencarian saat ini */

void record(int src, int dst) {
  int h, a;
  a = sumRow[src] - s[src];
  if (s[dst] - sumRow[dst] < a) a = s[dst] - sumRow[dst];
  for (h = 0; h < cur.len; h++)
    if (supply[cur.from[h]][cur.cl[h]] < a) a = supply[cur.from[h]][cur.cl[h]];
  if (a <= 0) return;
  cur.src = src;
  cur.dst = dst;
  cur.amt = a;
  cur.ok = 1;
  if (better( & cur, target)) * target = cur;
}

/* DFS rute: dari baris 'r' (sudah ada len hop). forcedCol >= 0 hanya untuk hop pertama. */
void dfs(int r, int cost, int used, int src, int forcedCol) {
  int j, k;
  if (cur.len > 0 && status[r] == 2) {
    int c0 = cur.cost;
    cur.cost = cost;
    record(src, r);
    cur.cost = c0;
    return;
  }
  if (cur.len >= MAXHOP) return;
  if (cur.len > 0 && status[r] != 1) return;
  for (j = 0; j < col; j++) {
    int h, dup = 0;
    if (forcedCol >= 0 && j != forcedCol) continue;
    if (!supply[r][j]) continue;
    for (h = 0; h < cur.len; h++)
      if (cur.cl[h] == j) dup = 1;
    if (dup) continue;
    for (k = 0; k < row; k++) {
      if (used & (1 << k)) continue;
      cur.from[cur.len] = r;
      cur.to[cur.len] = k;
      cur.cl[cur.len] = j;
      cur.len++;
      dfs(k, cost + matrix[k][j] - matrix[r][j], used | (1 << k), src, -1);
      cur.len--;
    }
  }
}

void printRoute(Route * R) {
  int h;
  for (h = 0; h < R -> len; h++) {
    if (h) printf(" , ");
    printf("baris %d -> baris %d (kolom %d)", R -> from[h] + 1, R -> to[h] + 1, R -> cl[h] + 1);
  }
}

int chooseMove(Route * best) {
  int i, j, any = 0;
  best -> ok = 0;
  printf("STEP 4: Diff setiap sel di baris ER (langsung / berantai)\n");
  for (i = 0; i < row; i++) {
    if (status[i] != 3) continue;
    for (j = 0; j < col; j++) {
      Route cell, direct, chain;
      int found = 0;
      if (!supply[i][j]) continue;
      memset( & cell, 0, sizeof(cell));
      memset( & direct, 0, sizeof(direct));
      memset( & chain, 0, sizeof(chain));
      memset( & cur, 0, sizeof(cur));
      target = & cell;
      dfs(i, 0, 1 << i, i, j);
      /* tampilan: hitung ulang terbaik langsung (len=1) dan berantai (len>=2) */
      {
        int k;
        for (k = 0; k < row; k++) {
          if (k == i || status[k] != 2) continue;
          int a = sumRow[i] - s[i];
          if (s[k] - sumRow[k] < a) a = s[k] - sumRow[k];
          if (supply[i][j] < a) a = supply[i][j];
          if (a <= 0) continue;
          Route t;
          memset( & t, 0, sizeof(t));
          t.ok = 1; t.cost = matrix[k][j] - matrix[i][j]; t.len = 1; t.src = i; t.dst = k; t.amt = a;
          t.from[0] = i; t.to[0] = k; t.cl[0] = j;
          if (better( & t, & direct)) direct = t;
        }
      }
      if (cell.ok && cell.len >= 2) chain = cell;
      printf("  Sel baris %d kolom %d (biaya %d, isi %d): ", i + 1, j + 1, matrix[i][j], supply[i][j]);
      if (direct.ok) printf("langsung ke baris %d Diff=%d", direct.dst + 1, direct.cost);
      else printf("langsung = -");
      if (MAXHOP >= 2) {
        if (chain.ok) {
          printf(" | berantai Diff=%d [", chain.cost);
          printRoute( & chain);
          printf("]");
        } else printf(" | berantai = -");
      }
      if (cell.ok) {
        printf("  => Diff sel = %d\n", cell.cost);
        found = 1;
      } else printf("\n");
      if (found) {
        any = 1;
        if (better( & cell, best)) * best = cell;
      }
    }
  }
  return any;
}

void check() {
  int i, j;
  char stat[][20] = {
    "SATISFIED",
    "NEED SUPPLY",
    "EXCESS ROW"
  };
  for (j = 0; j < col; j++) printf("\t");
  printf("\t\t\tsupply\t\tTC\tTCS\tTOTAL ALLOC\tSTATUS\n");
  for (j = 0; j < col; j++) printf("\t");
  printf("\t\t\t---------------------------------------------------------\n");
  for (i = 0; i < row; i++) {
    printf("Row ke %d\t", i + 1);
    for (j = 0; j < col; j++) printf("%d [%d]\t", matrix[i][j], supply[i][j]);
    printf("||\t%d\t\t%d\t%d\t%d\t\t%s\n\n", s[i], TC[i], CS[i], sumRow[i], stat[status[i] - 1]);
  }
  printf("Demand\t\t");
  for (j = 0; j < col; j++) printf("%d\t", demand[j]);
  printf("\n\n\n");
}

int compute() {
  FILE * inFile = fopen(inputName, "r");
  if (inFile == NULL) {
    printf("Error! Could not open file\n");
    return -1;
  }
  demand_index = 0;
  fscanf(inFile, "%d %d", & row, & col);
  int i, j;
  for (i = 0; i < row; i++) {
    TC[i] = 0;
    for (j = 0; j < col; j++) {
      fscanf(inFile, "%d", & matrix[i][j]);
      supply[i][j] = 0;
      TC[i] += matrix[i][j];
    }
    status[i] = 0;
    sumRow[i] = 0;
  }
  for (i = 0; i < row; i++) {
    fscanf(inFile, "%d", & s[i]);
    CS[i] = s[i] * TC[i];
  }
  minimumCell(inFile);
  fclose(inFile);

  updateStatus();
  int iteration = 1;
  check();
  while (!allSatisfied()) {
    Route best;
    int h;
    if (iteration > 200) {
      printf("CSM tidak konvergen (cek total supply = total demand)\n");
      return -1;
    }
    printf("=== Iterasi ke-%d ===\n", iteration++);
    if (!chooseMove( & best)) {
      printf("Soal Salah / tidak ada rute dari baris ER ke baris NR\n");
      return -1;
    }
    printf("\nSTEP 5: Sel dengan Diff terkecil = %d (baris ER %d, tujuan akhir baris %d)\n", best.cost, best.src + 1, best.dst + 1);
    printf("STEP 6: Pindahkan unit sepanjang rute: ");
    printRoute( & best);
    printf("\n        Jumlah unit = min(kelebihan baris %d = %d, kebutuhan baris %d = %d, isi sel rute) = %d\n\n",
      best.src + 1, sumRow[best.src] - s[best.src], best.dst + 1, s[best.dst] - sumRow[best.dst], best.amt);
    for (h = 0; h < best.len; h++) {
      supply[best.from[h]][best.cl[h]] -= best.amt;
      supply[best.to[h]][best.cl[h]] += best.amt;
      sumRow[best.from[h]] -= best.amt;
      sumRow[best.to[h]] += best.amt;
    }
    /* sumRow baris perantara: keluar & masuk sama banyak, jadi tetap */
    updateStatus();
    check();
  }
  {
    int t = totalCost();
    printf("TCC = %d\n\n", t);
    return t;
  }
}

void custom() {
  printf("Name = ");
  scanf("%s", inputName);
}

int cmpName(const void * a, const void * b) {
  return strcmp((const char * ) a, (const char * ) b);
}

/* jalankan semua file .txt di sebuah folder, tulis semua langkah + ringkasan ke csmBaru.txt */
void runFolder() {
  char folder[200], names[1000][100], path[320];
  int cnt = 0, i;
  DIR * d;
  struct dirent * e;
  FILE * f;
  int tcc[1000];
  printf("Folder = ");
  scanf("%199s", folder);
  d = opendir(folder);
  if (d == NULL) {
    printf("Folder tidak ditemukan\n");
    return;
  }
  while ((e = readdir(d)) != NULL && cnt < 1000) {
    size_t L = strlen(e -> d_name);
    if (L > 4 && L < 100 && strcmp(e -> d_name + L - 4, ".txt") == 0 && strcmp(e -> d_name, "csmBaru.txt") != 0)
      strcpy(names[cnt++], e -> d_name);
  }
  closedir(d);
  if (cnt == 0) {
    printf("Tidak ada file .txt di folder tersebut\n");
    return;
  }
  qsort(names, cnt, 100, cmpName);
  f = fopen("csmBaru.txt", "w");
  if (f == NULL) {
    printf("Tidak bisa membuat csmBaru.txt\n");
    return;
  }
  for (i = 0; i < cnt; i++) {
    sprintf(path, "%s/%s", folder, names[i]);
    strcpy(inputName, path);
    OUT = f;
    fprintf(f, "################ FILE: %s ################\n", names[i]);
    tcc[i] = compute();
    OUT = stdout;
    printf("(%d/%d) %s -> %s\n", i + 1, cnt, names[i], tcc[i] < 0 ? "ERROR" : "selesai");
  }
  fprintf(f, "################ RINGKASAN ################\n");
  for (i = 0; i < cnt; i++) {
    if (tcc[i] < 0) fprintf(f, "%-30s ERROR\n", names[i]);
    else fprintf(f, "%-30s TCC = %d\n", names[i], tcc[i]);
  }
  fclose(f);
  printf("Selesai. Hasil ada di csmBaru.txt\n");
}

int main() {
  int option = 0;
  OUT = stdout;
  printf("What is your input-file's name?\n1 = Default (input.txt)\n2 = Custom\n3 = Semua file .txt dalam satu folder (hasil -> csmBaru.txt)\n4 = Exit\n");
  printf("Option = ");
  scanf("%d", & option);
  if (option == 4) return 0;
  do {
    if (option == 1) {
      strcpy(inputName, "input.txt");
      compute();
    } else if (option == 2) {
      custom();
      compute();
    } else if (option == 3) {
      runFolder();
    } else if (option != 4) printf("Not Valid\n");
    printf("Option = ");
    scanf("%d", & option);
  } while (option != 4);
  printf("Program Stop\n\n");
  return 0;
}