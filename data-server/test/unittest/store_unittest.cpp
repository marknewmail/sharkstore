#include <gtest/gtest.h>

#include "base/util.h"
#include "helper/store_test_fixture.h"

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace {

using namespace sharkstore::test::helper;
using namespace sharkstore::dataserver;

// test base the account table
class StoreTest : public StoreTestFixture {
public:
    StoreTest() : StoreTestFixture(CreateAccountTable()) {}

    void InsertSomeRows() {
        for (int i = 1; i <= 100; ++i) {
            std::vector<std::string> row;
            row.push_back(std::to_string(i));

            char name[32] = {'\0'};
            snprintf(name, 32, "user-%04d", i);
            row.emplace_back(name);

            row.push_back(std::to_string(100 + i));
            rows_.push_back(std::move(row));
        }
        auto s = testInsert(rows_);
        ASSERT_TRUE(s.ok()) << s.ToString();
    }

protected:
    // inserted rows
    std::vector<std::vector<std::string>> rows_;
};

TEST_F(StoreTest, KeyValue) {
    // test put and get
    std::string key = sharkstore::randomString(32);
    std::string value = sharkstore::randomString(64);
    auto s = store_->Put(key, value);
    ASSERT_TRUE(s.ok());

    std::string actual_value;
    s = store_->Get(key, &actual_value);
    ASSERT_TRUE(s.ok());
    ASSERT_EQ(actual_value, value);

    // test delete and get
    s = store_->Delete(key);
    ASSERT_TRUE(s.ok());
    s = store_->Get(key, &actual_value);
    ASSERT_FALSE(s.ok());
    ASSERT_EQ(s.code(), sharkstore::Status::kNotFound);
}

TEST_F(StoreTest, Insert) {
    // one
    auto s = testInsert({{"1", "user1", "1.1"}});
    ASSERT_TRUE(s.ok()) << s.ToString();

    // multi
    {
        std::vector<std::vector<std::string>> rows;
        for (int i = 0; i < 100; ++i) {
            rows.push_back({std::to_string(i), "user", "1.1"});
        }
        auto s = testInsert(rows);
        ASSERT_TRUE(s.ok()) << s.ToString();
    }
    // insert check duplicate
    {
        InsertRequestBuilder builder(table_.get());
        builder.AddRow({"1", "user1", "100"});
        builder.SetCheckDuplicate();
        auto req = builder.Build();
        uint64_t affected = 0;
        auto s = store_->Insert(req, &affected);
        ASSERT_FALSE(s.ok());
        ASSERT_EQ(s.code(), sharkstore::Status::kDuplicate);
        ASSERT_EQ(affected, 0);
    }
}

TEST_F(StoreTest, SelectEmpty) {
    // all fields
    auto s = testSelect(
            [](SelectRequestBuilder& b) { b.AddAllFields(); },
            {});
    ASSERT_TRUE(s.ok()) << s.ToString();

    // random
    for (int i = 0; i < 100; ++i) {
        auto s = testSelect(
                [](SelectRequestBuilder& b) { b.AddRandomFields(); },
                {});
        ASSERT_TRUE(s.ok()) << s.ToString();
    }

    // select count
    s = testSelect(
            [](SelectRequestBuilder& b) { b.AddAggreFunc("count", ""); },
            {{"0"}});
    ASSERT_TRUE(s.ok()) << s.ToString();
}

TEST_F(StoreTest, SelectFields) {
    InsertSomeRows();

    // select all rows
    {
        auto s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAllFields();
                },
                rows_
        );
        ASSERT_TRUE(s.ok()) << s.ToString();
    }

    // Select one row per loop, all fields
    {
        for (size_t i = 0; i < rows_.size(); ++i) {
            auto s = testSelect(
                    [this, i](SelectRequestBuilder& b) {
                        b.AddAllFields();
                        b.SetKey({rows_[i][0]});
                    },
                    {rows_[i]}
            );
            ASSERT_TRUE(s.ok()) << s.ToString();
        }
    }

    // one filed
    {
        for (size_t i = 0; i < rows_.size(); ++i) {
            auto s = testSelect(
                    [this, i](SelectRequestBuilder& b) {
                        b.AddField("id");
                        b.SetKey({rows_[i][0]});
                    },
                    {{rows_[i][0]}}
            );
            ASSERT_TRUE(s.ok()) << s.ToString();
        }

        for (size_t i = 0; i < rows_.size(); ++i) {
            auto s = testSelect(
                    [this, i](SelectRequestBuilder& b) {
                        b.AddField("name");
                        b.SetKey({rows_[i][0]});
                    },
                    {{rows_[i][1]}}
            );
            ASSERT_TRUE(s.ok()) << s.ToString();
        }

        for (size_t i = 0; i < rows_.size(); ++i) {
            auto s = testSelect(
                    [this, i](SelectRequestBuilder& b) {
                        b.AddField("balance");
                        b.SetKey({rows_[i][0]});
                    },
                    {{rows_[i][2]}}
            );
            ASSERT_TRUE(s.ok()) << s.ToString();
        }
    }

    // no pk fileds
    {
        for (size_t i = 0; i < rows_.size(); ++i) {
            auto s = testSelect(
                    [this, i](SelectRequestBuilder& b) {
                        b.AddField("name");
                        b.AddField("balance");
                        b.SetKey({rows_[i][0]});
                    },
                    {{rows_[i][1], rows_[i][2]}}
            );
            ASSERT_TRUE(s.ok()) << s.ToString();
        }
    }
}

TEST_F(StoreTest, SelectScope) {
    InsertSomeRows();

    // scope: [2-4)
    auto s = testSelect(
            [](SelectRequestBuilder& b) {
                b.AddAllFields();
                b.SetScope({"2"}, {"4"});
            },
            {rows_[1], rows_[2]}
    );
    ASSERT_TRUE(s.ok()) << s.ToString();

    // scope: [2-
    s = testSelect(
            [](SelectRequestBuilder& b) {
                b.AddAllFields();
                b.SetScope({"2"}, {});
            },
            {rows_.cbegin() + 1, rows_.cend()}
    );
    ASSERT_TRUE(s.ok()) << s.ToString();

    // scope: -4)
    s = testSelect(
            [](SelectRequestBuilder& b) {
                b.AddAllFields();
                b.SetScope({}, {"4"});
            },
            {rows_[0], rows_[1], rows_[2]}
    );
    ASSERT_TRUE(s.ok()) << s.ToString();
}

TEST_F(StoreTest, SelectLimit) {
    InsertSomeRows();

    auto s = testSelect(
            [](SelectRequestBuilder& b) {
                b.AddAllFields();
                b.AddLimit(3);
            },
            {rows_[0], rows_[1], rows_[2]}
    );
    ASSERT_TRUE(s.ok()) << s.ToString();

    s = testSelect(
            [](SelectRequestBuilder& b) {
                b.AddAllFields();
                b.AddLimit(3, 1);
            },
            {rows_[1], rows_[2], rows_[3]}
    );
    ASSERT_TRUE(s.ok()) << s.ToString();
}

TEST_F(StoreTest, SelectWhere) {
    InsertSomeRows();

    // select * where id
    {
        // id == 1
        auto s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAllFields();
                    b.AddMatch("id", kvrpcpb::Equal, "1");
                },
                {rows_[0]}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();

        // id != 1
        s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAllFields();
                    b.AddMatch("id", kvrpcpb::NotEqual, "1");
                },
                {rows_.cbegin() + 1, rows_.cend()}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();

        // id < 3
        s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAllFields();
                    b.AddMatch("id", kvrpcpb::Less, "3");
                },
                {rows_[0], rows_[1]}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();

        // id <= 2
        s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAllFields();
                    b.AddMatch("id", kvrpcpb::LessOrEqual, "2");
                },
                {rows_[0], rows_[1]}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();

        // id > 2
        s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAllFields();
                    b.AddMatch("id", kvrpcpb::Larger, "2");
                },
                {rows_.cbegin() + 2, rows_.cend()}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();

        // id >= 2
        s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAllFields();
                    b.AddMatch("id", kvrpcpb::LargerOrEqual, "2");
                },
                {rows_.cbegin() + 1, rows_.cend()}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();

        // id > 1 and id < 4
        s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAllFields();
                    b.AddMatch("id", kvrpcpb::Larger, "1");
                    b.AddMatch("id", kvrpcpb::Less, "4");
                },
                {rows_[1], rows_[2]}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();

        // id > 4 and id < 1
        s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAllFields();
                    b.AddMatch("id", kvrpcpb::Larger, "4");
                    b.AddMatch("id", kvrpcpb::Less, "1");
                },
                {}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();
    }

    // select where name
    {
        auto s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAllFields();
                    b.AddMatch("name", kvrpcpb::Larger, "user-0002");
                },
                {rows_.cbegin() + 2, rows_.cend()}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();
    }
    // select where balance
    {
        auto s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAllFields();
                    b.AddMatch("balance", kvrpcpb::Less, "103");
                },
                {rows_[0], rows_[1]}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();
    }
}

TEST_F(StoreTest, SelectAggreCount) {
    InsertSomeRows();

    // select count(*)
    {
        auto s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAggreFunc("count", "");
                },
                {{std::to_string(rows_.size())}}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();
    }

    // select count(*) where id = {id}
    {
        for (size_t i = 0; i < rows_.size(); ++i) {
            auto s = testSelect(
                    [this, i](SelectRequestBuilder& b) {
                        b.SetKey({rows_[i][0]});
                        b.AddAggreFunc("count", "");
                    },
                    {{"1"}}
            );
            ASSERT_TRUE(s.ok()) << s.ToString();
        }

        // not exist
        auto s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.SetKey({std::to_string(std::numeric_limits<int64_t>::max())});
                    b.AddAggreFunc("count", "");
                },
                {{"0"}}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();
    }

    // select count(*) where id
    {
        auto s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAggreFunc("count", "");
                    b.AddMatch("id", kvrpcpb::Equal, "1");
                },
                {{"1"}}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();
    }
    {
        auto s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAggreFunc("count", "");
                    b.AddMatch("id", kvrpcpb::NotEqual, "1");
                },
                {{std::to_string(rows_.size() - 1)}}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();
    }
    {
        auto s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAggreFunc("count", "");
                    b.AddMatch("id", kvrpcpb::Less, "5");
                },
                {{"4"}}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();
    }
    {
        auto s = testSelect(
                [](SelectRequestBuilder& b) {
                    b.AddAggreFunc("count", "");
                    b.AddMatch("id", kvrpcpb::Larger, "5");
                },
                {{std::to_string(rows_.size() - 5)}}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();
    }
}

TEST_F(StoreTest, SelectAggreMore) {
    InsertSomeRows();

    // select aggre id
    {
        // max
        auto s = testSelect(
                [](SelectRequestBuilder &b) {
                    b.AddAggreFunc("max", "id");
                },
                {{"100"}}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();

        // min
        s = testSelect(
                [](SelectRequestBuilder &b) {
                    b.AddAggreFunc("min", "id");
                },
                {{"1"}}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();

        int sum = 0;
        for (int i = 1; i <= 100; ++i) {
            sum += i;
        }
        // sum
        s = testSelect(
                [](SelectRequestBuilder &b) {
                    b.AddAggreFunc("sum", "id");
                },
                {{std::to_string(sum)}}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();
    }

    // banlance
    {
        // max
        auto s = testSelect(
                [](SelectRequestBuilder &b) {
                    b.AddAggreFunc("max", "balance");
                },
                {{"200"}}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();

        // min
        s = testSelect(
                [](SelectRequestBuilder &b) {
                    b.AddAggreFunc("min", "balance");
                },
                {{"101"}}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();

        int sum = 0;
        for (int i = 1; i <= 100; ++i) {
            sum += i;
            sum += 100;
        }
        // sum
        s = testSelect(
                [](SelectRequestBuilder &b) {
                    b.AddAggreFunc("sum", "balance");
                },
                {{std::to_string(sum)}}
        );
        ASSERT_TRUE(s.ok()) << s.ToString();
    }
}

TEST_F(StoreTest, DeleteBasic) {
    InsertSomeRows();

    // delete 1
    auto s = testDelete(
            [](DeleteRequestBuilder& b) {
                b.SetKey({"1"});
            },
            1
    );
    ASSERT_TRUE(s.ok()) << s.ToString();

    s = testSelect(
            [](SelectRequestBuilder& b) {
                b.AddAllFields();
            },
            {rows_.cbegin() + 1, rows_.cend()}
    );
    ASSERT_TRUE(s.ok()) << s.ToString();


    // delete scope
    s = testDelete(
            [](DeleteRequestBuilder& b) {
                b.SetScope({"2"}, {"5"});
            },
            3
    );
    ASSERT_TRUE(s.ok()) << s.ToString();

    s = testSelect(
            [](SelectRequestBuilder& b) {
                b.AddAllFields();
            },
            {rows_.cbegin() + 4, rows_.cend()}
    );
    ASSERT_TRUE(s.ok()) << s.ToString();


    // delete all
    s = testDelete(
            [](DeleteRequestBuilder& b) {
            },
            rows_.size() - 4
    );
    ASSERT_TRUE(s.ok()) << s.ToString();

    s = testSelect(
            [](SelectRequestBuilder& b) {
                b.AddAllFields();
            },
            {});
    ASSERT_TRUE(s.ok()) << s.ToString();
}

TEST_F(StoreTest, DeleteWhere) {
    InsertSomeRows();

    sharkstore::Status s;

    // delete id == 1
    s = testDelete(
            [](DeleteRequestBuilder& b) {
                b.AddMatch("id", kvrpcpb::Equal, "1");
            },
            1
    );
    ASSERT_TRUE(s.ok()) << s.ToString();

    s = testSelect(
            [](SelectRequestBuilder& b) {
                b.AddAllFields();
            },
            {rows_.cbegin() + 1, rows_.cend()}
    );
    ASSERT_TRUE(s.ok()) << s.ToString();


    // delete name == "user-0002"
    s = testDelete(
            [](DeleteRequestBuilder& b) {
                b.AddMatch("name", kvrpcpb::Equal, "user-0002");
            },
            1
    );
    ASSERT_TRUE(s.ok()) << s.ToString();

    s = testSelect(
            [](SelectRequestBuilder& b) {
                b.AddAllFields();
            },
            {rows_.cbegin() + 2, rows_.cend()}
    );
    ASSERT_TRUE(s.ok()) << s.ToString();

    // delete balance < 100 + 5
    s = testDelete(
            [](DeleteRequestBuilder& b) {
                b.AddMatch("balance", kvrpcpb::Less, "105");
            },
            2
    );
    ASSERT_TRUE(s.ok()) << s.ToString();

    s = testSelect(
            [](SelectRequestBuilder& b) {
                b.AddAllFields();
            },
            {rows_.cbegin() + 4, rows_.cend()}
    );
    ASSERT_TRUE(s.ok()) << s.ToString();
}


} /* namespace  */
