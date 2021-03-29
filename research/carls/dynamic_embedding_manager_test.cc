/*Copyright 2020 Google LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

     https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "research/carls/dynamic_embedding_manager.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_format.h"
#include "research/carls/base/file_helper.h"
#include "research/carls/base/proto_helper.h"
#include "research/carls/kbs_server_helper.h"

namespace carls {

using ::tensorflow::Tensor;
using ::tensorflow::TensorShape;
using ::tensorflow::tstring;

class DynamicEmbeddingManagerTest : public ::testing::Test {
 protected:
  DynamicEmbeddingManagerTest() {}

  DynamicEmbeddingConfig BuildConfig(const int dimension,
                                     const float learning_rate = 0.1f) {
    return ParseTextProtoOrDie<DynamicEmbeddingConfig>(
        absl::StrFormat(R"(
        embedding_dimension: %d
        knowledge_bank_config {
          initializer { zero_initializer {} }
          extension {
            [type.googleapis.com/carls.InProtoKnowledgeBankConfig] {}
          }
        }
        gradient_descent_config {
          learning_rate: %f
          sgd {}
        }
      )",
                        dimension, learning_rate));
  }
};

TEST_F(DynamicEmbeddingManagerTest, Create) {
  KnowledgeBankServiceOptions options;
  KbsServerHelper helper(options);
  const std::string address = absl::StrCat("localhost:", helper.port());
  DynamicEmbeddingConfig config = BuildConfig(/*dimension=*/10);
  // Empty address.
  EXPECT_TRUE(DynamicEmbeddingManager::Create(config, "emb",
                                              /*kbs_address=*/"") == nullptr);
  // Invalid config.
  DynamicEmbeddingConfig empty_config;
  EXPECT_TRUE(DynamicEmbeddingManager::Create(empty_config, "emb",
                                              /*kbs_address=*/"") == nullptr);
  // A valid case.
  auto de_manager = DynamicEmbeddingManager::Create(config, "emb", address);
  EXPECT_TRUE(de_manager != nullptr);
}

TEST_F(DynamicEmbeddingManagerTest, Lookup_EmptyInput) {
  KnowledgeBankServiceOptions options;
  KbsServerHelper helper(options);
  const std::string address = absl::StrCat("localhost:", helper.port());
  DynamicEmbeddingConfig config = BuildConfig(/*dimension=*/10);
  auto de_manager = DynamicEmbeddingManager::Create(config, "emb", address);
  ASSERT_TRUE(de_manager != nullptr);

  Tensor empty_keys;
  Tensor output;
  auto status = de_manager->Lookup(empty_keys, /*update=*/true, &output);
  EXPECT_EQ("No input.", status.message());
}

TEST_F(DynamicEmbeddingManagerTest, Lookup_1DInput) {
  KnowledgeBankServiceOptions options;
  KbsServerHelper helper(options);
  std::string address = absl::StrCat("localhost:", helper.port());
  DynamicEmbeddingConfig config = BuildConfig(/*dimension=*/2);
  auto de_manager = DynamicEmbeddingManager::Create(config, "emb", address);
  ASSERT_TRUE(de_manager != nullptr);

  Tensor keys(tensorflow::DT_STRING, TensorShape({2}));
  auto keys_value = keys.vec<tstring>();
  keys_value(0) = "first";
  keys_value(1) = "";
  Tensor output(tensorflow::DT_FLOAT, TensorShape({2, 2}));
  ASSERT_TRUE(de_manager->Lookup(keys, /*update=*/true, &output).ok());
  auto output_values = output.matrix<float>();
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      EXPECT_FLOAT_EQ(0, output_values(i, j));
    }
  }
}

TEST_F(DynamicEmbeddingManagerTest, Lookup_2DInput) {
  KnowledgeBankServiceOptions options;
  KbsServerHelper helper(options);
  std::string address = absl::StrCat("localhost:", helper.port());
  DynamicEmbeddingConfig config = BuildConfig(/*dimension=*/2);
  auto de_manager = DynamicEmbeddingManager::Create(config, "emb", address);
  ASSERT_TRUE(de_manager != nullptr);

  Tensor keys(tensorflow::DT_STRING, TensorShape({2, 2}));
  auto keys_value = keys.matrix<tstring>();
  keys_value(0, 0) = "first";
  keys_value(0, 1) = "second";
  keys_value(1, 0) = "third";
  keys_value(1, 1) = "";
  Tensor output = Tensor(tensorflow::DT_FLOAT, TensorShape({2, 2, 2}));
  ASSERT_TRUE(de_manager->Lookup(keys, /*update=*/true, &output).ok());
  auto output_values = output.tensor<float, 3>();
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      for (int k = 0; k < 2; ++k) {
        EXPECT_FLOAT_EQ(0, output_values(i, j, k));
      }
    }
  }
}

TEST_F(DynamicEmbeddingManagerTest, UpdateValues_InvalidInputs) {
  KnowledgeBankServiceOptions options;
  KbsServerHelper helper(options);
  std::string address = absl::StrCat("localhost:", helper.port());
  DynamicEmbeddingConfig config = BuildConfig(/*dimension=*/2);
  auto de_manager = DynamicEmbeddingManager::Create(config, "emb", address);
  ASSERT_TRUE(de_manager != nullptr);

  // Empty input.
  Tensor keys;
  Tensor values;
  EXPECT_EQ("Input key is empty.",
            de_manager->UpdateValues(keys, values).message());

  // Inconsistent key size and value size.
  keys = Tensor(tensorflow::DT_STRING, TensorShape({3}));
  values = Tensor(tensorflow::DT_STRING, TensorShape({2, 2}));
  EXPECT_EQ("Inconsistent keys size and values size: 3 v.s. 2",
            de_manager->UpdateValues(keys, values).message());

  // Inconsistent embedding dimension.
  values = Tensor(tensorflow::DT_STRING, TensorShape({3, 4}));
  EXPECT_EQ("Inconsistent embedding dimension, got 4 expect 2",
            de_manager->UpdateValues(keys, values).message());
}

TEST_F(DynamicEmbeddingManagerTest, UpdateValues_1DInput) {
  KnowledgeBankServiceOptions options;
  KbsServerHelper helper(options);
  std::string address = absl::StrCat("localhost:", helper.port());
  DynamicEmbeddingConfig config = BuildConfig(/*dimension=*/2);
  auto de_manager = DynamicEmbeddingManager::Create(config, "emb", address);
  ASSERT_TRUE(de_manager != nullptr);

  Tensor keys(tensorflow::DT_STRING, TensorShape({3}));
  Tensor embed(tensorflow::DT_FLOAT, TensorShape({3, 2}));
  auto keys_value = keys.vec<tstring>();
  keys_value(0) = "first";
  keys_value(1) = "second";
  keys_value(2) = "third";
  auto embed_value = embed.matrix<float>();
  embed_value(0, 0) = -1;
  embed_value(0, 1) = 3;
  embed_value(1, 0) = 2;
  embed_value(1, 1) = -10;
  embed_value(2, 0) = -5;
  embed_value(2, 1) = 1;
  ASSERT_TRUE(de_manager->UpdateValues(keys, embed).ok());

  // Check results.
  Tensor output = Tensor(tensorflow::DT_FLOAT, TensorShape({3, 2}));
  ASSERT_TRUE(de_manager->Lookup(keys, /*update=*/false, &output).ok());
  auto output_values = output.matrix<float>();
  EXPECT_FLOAT_EQ(-1, output_values(0, 0));
  EXPECT_FLOAT_EQ(3, output_values(0, 1));
  EXPECT_FLOAT_EQ(2, output_values(1, 0));
  EXPECT_FLOAT_EQ(-10, output_values(1, 1));
  EXPECT_FLOAT_EQ(-5, output_values(2, 0));
  EXPECT_FLOAT_EQ(1, output_values(2, 1));
}

TEST_F(DynamicEmbeddingManagerTest, UpdateValues_2DInput) {
  KnowledgeBankServiceOptions options;
  KbsServerHelper helper(options);
  std::string address = absl::StrCat("localhost:", helper.port());
  DynamicEmbeddingConfig config = BuildConfig(/*dimension=*/2);
  auto de_manager = DynamicEmbeddingManager::Create(config, "emb", address);
  ASSERT_TRUE(de_manager != nullptr);

  Tensor keys(tensorflow::DT_STRING, TensorShape({2, 2}));
  Tensor embed(tensorflow::DT_FLOAT, TensorShape({2, 2, 2}));
  auto keys_value = keys.matrix<tstring>();
  keys_value(0, 0) = "first";
  keys_value(0, 1) = "second";
  keys_value(1, 0) = "third";
  keys_value(1, 1) = "";
  auto embed_values = embed.tensor<float, 3>();
  int val = 0;
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      for (int k = 0; k < 2; ++k) {
        embed_values(i, j, k) = val++;
      }
    }
  }
  ASSERT_TRUE(de_manager->UpdateValues(keys, embed).ok());

  // Check results.
  Tensor output = Tensor(tensorflow::DT_FLOAT, TensorShape({2, 2, 2}));
  ASSERT_TRUE(de_manager->Lookup(keys, /*update=*/false, &output).ok());
  auto output_values = output.tensor<float, 3>();
  EXPECT_FLOAT_EQ(0, output_values(0, 0, 0));
  EXPECT_FLOAT_EQ(1, output_values(0, 0, 1));
  EXPECT_FLOAT_EQ(2, output_values(0, 1, 0));
  EXPECT_FLOAT_EQ(3, output_values(0, 1, 1));
  EXPECT_FLOAT_EQ(4, output_values(1, 0, 0));
  EXPECT_FLOAT_EQ(5, output_values(1, 0, 1));
  // For empty input, it returns all zeros.
  EXPECT_FLOAT_EQ(0, output_values(1, 1, 0));
  EXPECT_FLOAT_EQ(0, output_values(1, 1, 1));
}

TEST_F(DynamicEmbeddingManagerTest, UpdateGradients_1DInput) {
  KnowledgeBankServiceOptions options;
  KbsServerHelper helper(options);
  const std::string address = absl::StrCat("localhost:", helper.port());
  DynamicEmbeddingConfig config = BuildConfig(/*dimension=*/2);
  auto de_manager = DynamicEmbeddingManager::Create(config, "emb", address);
  ASSERT_TRUE(de_manager != nullptr);

  Tensor keys(tensorflow::DT_STRING, TensorShape({3}));
  auto keys_value = keys.vec<tstring>();
  keys_value(0) = "first";
  keys_value(1) = "second";
  keys_value(2) = "third";
  // Initial update returns all zeros.
  Tensor embed = Tensor(tensorflow::DT_FLOAT, TensorShape({3, 2}));
  ASSERT_TRUE(de_manager->Lookup(keys, /*update=*/true, &embed).ok());

  // Updates the gradients using SGD.
  Tensor grads(tensorflow::DT_FLOAT, TensorShape({3, 2}));
  auto grads_values = grads.matrix<float>();
  grads_values(0, 0) = 1;
  grads_values(0, 1) = 2;
  grads_values(1, 0) = 3;
  grads_values(1, 1) = 4;
  grads_values(2, 0) = 5;
  grads_values(2, 1) = 6;
  ASSERT_TRUE(de_manager->UpdateGradients(keys, grads).ok());

  // Check results with learning rate set to 0.1.
  ASSERT_TRUE(de_manager->Lookup(keys, /*update=*/false, &embed).ok());
  auto embed_values = embed.matrix<float>();
  EXPECT_FLOAT_EQ(-0.1, embed_values(0, 0));
  EXPECT_FLOAT_EQ(-0.2, embed_values(0, 1));
  EXPECT_FLOAT_EQ(-0.3, embed_values(1, 0));
  EXPECT_FLOAT_EQ(-0.4, embed_values(1, 1));
  EXPECT_FLOAT_EQ(-0.5, embed_values(2, 0));
  EXPECT_FLOAT_EQ(-0.6, embed_values(2, 1));
}

TEST_F(DynamicEmbeddingManagerTest, UpdateGradients_2DInput) {
  KnowledgeBankServiceOptions options;
  KbsServerHelper helper(options);
  std::string address = absl::StrCat("localhost:", helper.port());
  DynamicEmbeddingConfig config = BuildConfig(/*dimension=*/2);
  auto de_manager = DynamicEmbeddingManager::Create(config, "emb", address);
  ASSERT_TRUE(de_manager != nullptr);

  Tensor keys(tensorflow::DT_STRING, TensorShape({2, 2}));
  auto keys_value = keys.matrix<tstring>();
  keys_value(0, 0) = "first";
  keys_value(0, 1) = "second";
  keys_value(1, 0) = "third";
  keys_value(1, 1) = "";
  // Initial update returns all zeros.
  Tensor embed(tensorflow::DT_FLOAT, TensorShape({2, 2, 2}));
  ASSERT_TRUE(de_manager->Lookup(keys, /*update=*/true, &embed).ok());

  Tensor grads(tensorflow::DT_FLOAT, TensorShape({2, 2, 2}));
  auto grads_values = grads.tensor<float, 3>();
  int val = 0;
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      for (int k = 0; k < 2; ++k) {
        grads_values(i, j, k) = val++;
      }
    }
  }
  ASSERT_TRUE(de_manager->UpdateGradients(keys, grads).ok());

  // Check results with learning rate set to 0.1.
  ASSERT_TRUE(de_manager->Lookup(keys, /*update=*/false, &embed).ok());
  auto embed_values = embed.tensor<float, 3>();
  EXPECT_FLOAT_EQ(0, embed_values(0, 0, 0));
  EXPECT_FLOAT_EQ(-0.1, embed_values(0, 0, 1));
  EXPECT_FLOAT_EQ(-0.2, embed_values(0, 1, 0));
  EXPECT_FLOAT_EQ(-0.3, embed_values(0, 1, 1));
  EXPECT_FLOAT_EQ(-0.4, embed_values(1, 0, 0));
  EXPECT_FLOAT_EQ(-0.5, embed_values(1, 0, 1));
  // For empty input, it returns all zeros.
  EXPECT_FLOAT_EQ(0, embed_values(1, 1, 0));
  EXPECT_FLOAT_EQ(0, embed_values(1, 1, 1));
}

TEST_F(DynamicEmbeddingManagerTest, ImportAndExport) {
  KnowledgeBankServiceOptions options;
  KbsServerHelper helper(options);
  std::string address = absl::StrCat("localhost:", helper.port());
  DynamicEmbeddingConfig config = BuildConfig(/*dimension=*/2);
  auto de_manager = DynamicEmbeddingManager::Create(config, "emb", address);
  ASSERT_TRUE(de_manager != nullptr);

  // Add a few keys.
  Tensor keys(tensorflow::DT_STRING, TensorShape({3}));
  auto keys_value = keys.vec<tstring>();
  keys_value(0) = "first";
  keys_value(1) = "second";
  keys_value(2) = "third";
  // Initial update returns all zeros.
  Tensor embed = Tensor(tensorflow::DT_FLOAT, TensorShape({3, 2}));
  ASSERT_TRUE(de_manager->Lookup(keys, /*update=*/true, &embed).ok());

  // Export.
  std::string exported_path;
  ASSERT_TRUE(de_manager->Export(testing::TempDir(), &exported_path).ok());
  EXPECT_EQ(JoinPath(testing::TempDir(), "emb/embedding_store_meta_data.pbtxt"),
            exported_path);

  // Update the embeddings of a few keys.
  Tensor new_embed(tensorflow::DT_FLOAT, TensorShape({3, 2}));
  auto new_embed_value = new_embed.matrix<float>();
  new_embed_value(0, 0) = 1;
  new_embed_value(0, 1) = 2;
  new_embed_value(1, 0) = 3;
  new_embed_value(1, 1) = 4;
  new_embed_value(2, 0) = 5;
  new_embed_value(2, 1) = 6;
  ASSERT_TRUE(de_manager->UpdateValues(keys, new_embed).ok());

  // Now restore to previous state.
  ASSERT_TRUE(de_manager->Import(exported_path).ok());

  // Checks the results.
  ASSERT_TRUE(de_manager->Lookup(keys, /*update=*/false, &new_embed).ok());
  auto embed_value = embed.matrix<float>();
  new_embed_value = new_embed.matrix<float>();
  EXPECT_EQ(embed_value(0, 0), new_embed_value(0, 0));
  EXPECT_EQ(embed_value(0, 1), new_embed_value(0, 1));
  EXPECT_EQ(embed_value(1, 0), new_embed_value(1, 0));
  EXPECT_EQ(embed_value(1, 1), new_embed_value(1, 1));
  EXPECT_EQ(embed_value(2, 0), new_embed_value(2, 0));
  EXPECT_EQ(embed_value(2, 1), new_embed_value(2, 1));
}

}  // namespace carls
