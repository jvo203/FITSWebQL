#include <ipp.h>

#include <fstream>  // ifstream
#include <iostream> // cout, cerr
#include <sstream>  // stringstream
using namespace std;

int main() {
  int x = 0, y = 0, width = 0, height = 0;
  ifstream infile("zero.pgm");
  stringstream ss;
  string inputLine = "";

  // First line : version
  getline(infile, inputLine);
  if (inputLine.compare("P5") != 0)
    cerr << "Version error" << endl;
  else
    cout << "Version : " << inputLine << endl;

  // Second line : comment
  getline(infile, inputLine);
  cout << "Comment : " << inputLine << endl;

  // Continue with a stringstream
  ss << infile.rdbuf();
  // Third line : size
  ss >> width >> height;
  cout << width << " x " << height << endl;

  uint8_t array[height][width];

  // Following lines : data
  for (y = 0; y < height; y++)
    for (x = 0; x < width; x++)
      ss >> array[y][x];

  // Now print the array to see the result
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      cout << (int)array[y][x] << " ";
    }
    cout << endl;
  }
  infile.close();
}