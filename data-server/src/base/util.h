_Pragma("once");

#include <sys/types.h>
#include <cstdint>
#include <string>
#include <vector>

namespace sharkstore {

int randomInt();

std::string randomString(size_t length);

std::string strErrno(int errno_copy);

std::string EncodeToHex(const std::string& src);
bool DecodeFromHex(const std::string& hex, std::string* result);

std::string SliceSeparate(const std::string &l, const std::string &r, size_t max_len = 0);

std::string JoinFilePath(const std::vector<std::string> &strs);

int CheckDirExist(const std::string& path);
int MakeDirAll(const std::string &path, mode_t mode);
int RemoveDirAll(const char *name);

void AnnotateThread(pthread_t handle, const char *name);

int ParseBytesValue(const char* str, int64_t* value);

} /* namespace sharkstore */
