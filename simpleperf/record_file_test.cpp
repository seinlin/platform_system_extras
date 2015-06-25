/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include <string.h>
#include "environment.h"
#include "event_attr.h"
#include "event_fd.h"
#include "event_type.h"
#include "record.h"
#include "record_file.h"

#include "record_equal_test.h"

using namespace PerfFileFormat;

class RecordFileTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    filename = "temporary.record_file";
    std::unique_ptr<EventTypeAndModifier> event_type_modifier = ParseEventType("cpu-cycles");
    ASSERT_TRUE(event_type_modifier != nullptr);
    event_attr = CreateDefaultPerfEventAttr(event_type_modifier->event_type);
    event_attr.sample_id_all = 1;
    event_attr.sample_type |= PERF_SAMPLE_TIME;
    std::unique_ptr<EventFd> event_fd = EventFd::OpenEventFile(event_attr, getpid(), -1);
    ASSERT_TRUE(event_fd != nullptr);
    event_fds.push_back(std::move(event_fd));
  }

  std::string filename;
  perf_event_attr event_attr;
  std::vector<std::unique_ptr<EventFd>> event_fds;
};

TEST_F(RecordFileTest, smoke) {
  // Write to a record file.
  std::unique_ptr<RecordFileWriter> writer =
      RecordFileWriter::CreateInstance(filename, event_attr, event_fds);
  ASSERT_TRUE(writer != nullptr);

  // Write data section.
  MmapRecord mmap_record =
      CreateMmapRecord(event_attr, true, 1, 1, 0x1000, 0x2000, 0x3000, "mmap_record_example");
  ASSERT_TRUE(writer->WriteData(mmap_record.BinaryFormat()));

  // Write feature section.
  ASSERT_TRUE(writer->WriteFeatureHeader(1));
  BuildId build_id;
  for (size_t i = 0; i < build_id.size(); ++i) {
    build_id[i] = i;
  }
  BuildIdRecord build_id_record = CreateBuildIdRecord(false, getpid(), build_id, "init");
  ASSERT_TRUE(writer->WriteBuildIdFeature({build_id_record}));
  ASSERT_TRUE(writer->Close());

  // Read from a record file.
  std::unique_ptr<RecordFileReader> reader = RecordFileReader::CreateInstance(filename);
  ASSERT_TRUE(reader != nullptr);
  const FileHeader* file_header = reader->FileHeader();
  ASSERT_TRUE(file_header != nullptr);
  std::vector<const FileAttr*> attrs = reader->AttrSection();
  ASSERT_EQ(1u, attrs.size());
  ASSERT_EQ(0, memcmp(&attrs[0]->attr, &event_attr, sizeof(perf_event_attr)));
  std::vector<uint64_t> ids = reader->IdsForAttr(attrs[0]);
  ASSERT_EQ(1u, ids.size());

  // Read and check data section.
  std::vector<std::unique_ptr<const Record>> records = reader->DataSection();
  ASSERT_EQ(1u, records.size());
  CheckRecordEqual(mmap_record, *records[0]);

  // Read and check feature section.
  ASSERT_TRUE(file_header->features[FEAT_BUILD_ID / 8] & (1 << (FEAT_BUILD_ID % 8)));
  std::vector<SectionDesc> sections = reader->FeatureSectionDescriptors();
  ASSERT_EQ(1u, sections.size());
  const perf_event_header* header =
      reinterpret_cast<const perf_event_header*>(reader->DataAtOffset(sections[0].offset));
  ASSERT_TRUE(header != nullptr);
  ASSERT_EQ(sections[0].size, header->size);
  CheckRecordEqual(build_id_record, BuildIdRecord(header));

  ASSERT_TRUE(reader->Close());
}

TEST_F(RecordFileTest, records_sorted_by_time) {
  // Write to a record file;
  std::unique_ptr<RecordFileWriter> writer =
      RecordFileWriter::CreateInstance(filename, event_attr, event_fds);
  ASSERT_TRUE(writer != nullptr);

  // Write data section.
  MmapRecord r1 = CreateMmapRecord(event_attr, true, 1, 1, 0x100, 0x2000, 0x3000, "mmap_record1");
  MmapRecord r2 = r1;
  MmapRecord r3 = r1;
  r1.sample_id.time_data.time = 2;
  r2.sample_id.time_data.time = 1;
  r3.sample_id.time_data.time = 3;
  ASSERT_TRUE(writer->WriteData(r1.BinaryFormat()));
  ASSERT_TRUE(writer->WriteData(r2.BinaryFormat()));
  ASSERT_TRUE(writer->WriteData(r3.BinaryFormat()));
  ASSERT_TRUE(writer->Close());

  // Read from a record file.
  std::unique_ptr<RecordFileReader> reader = RecordFileReader::CreateInstance(filename);
  ASSERT_TRUE(reader != nullptr);
  std::vector<std::unique_ptr<const Record>> records = reader->DataSection();
  ASSERT_EQ(3u, records.size());
  CheckRecordEqual(r2, *records[0]);
  CheckRecordEqual(r1, *records[1]);
  CheckRecordEqual(r3, *records[2]);

  ASSERT_TRUE(reader->Close());
}