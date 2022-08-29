#include <cmath>
#include <cstring>

#include "gtest/gtest.h"

extern "C"
{
#include "../dict.h"
}

class HashTableCreationTest : public testing::Test
{
protected:
  void
  SetUp () override
  {
  }

  void
  TearDown () override
  {
  }
};

TEST (HashTableCreation, EmptyDictCreate)
{
  dict *dt = dict_new_empty ();
  EXPECT_TRUE (dt != NULL);
}