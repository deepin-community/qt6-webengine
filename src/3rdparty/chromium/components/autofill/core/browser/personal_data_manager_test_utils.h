// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_PERSONAL_DATA_MANAGER_TEST_UTILS_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_PERSONAL_DATA_MANAGER_TEST_UTILS_H_

#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "components/autofill/core/browser/personal_data_manager_observer.h"
#include "components/autofill/core/browser/webdata/autofill_webdata_service.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace autofill {

class PersonalDataManager;

class PersonalDataLoadedObserverMock : public PersonalDataManagerObserver {
 public:
  PersonalDataLoadedObserverMock();
  ~PersonalDataLoadedObserverMock() override;

  MOCK_METHOD(void, OnPersonalDataChanged, (), (override));
  MOCK_METHOD(void, OnPersonalDataFinishedProfileTasks, (), (override));
};

// Helper class to wait for a `OnPersonalDataFinishedProfileTasks()` call from
// the `pdm`. This is necessary, since the PDM operates asynchronously on the
// WebDatabase.
// Additional expectations can be set using `mock_observer()`.
// Example usage:
//   PersonalDataManagerWaiter waiter(pdm);
//   EXPECT_CALL(waiter.mock_observer(), OnPersonalDataChanged()).Times(1);
//   pdm.AddProfile(AutofillProfile());
//   waiter.Wait();

// Initializing the waiter after the operation (`AddProfile()`, in this case) is
// not recommended, because the notifications might fire before the expectations
// are set.
class PersonalDataProfileTaskWaiter {
 public:
  explicit PersonalDataProfileTaskWaiter(PersonalDataManager& pdm);
  ~PersonalDataProfileTaskWaiter();

  // Waits for `OnPersonalDataFinishedProfileTasks()` to trigger. As a safety
  // mechanism, this can only be called once per `PersonalDataProfileTaskWaiter`
  // instance. This is because gMock doesn't support setting expectations after
  // a function (here the mock_observer_'s
  // `OnPersonalDataFinishedProfileTasks()`) was called.
  void Wait() &&;

  PersonalDataLoadedObserverMock& mock_observer() { return mock_observer_; }

 private:
  testing::NiceMock<PersonalDataLoadedObserverMock> mock_observer_;
  base::RunLoop run_loop_;
  base::ScopedObservation<PersonalDataManager, PersonalDataLoadedObserverMock>
      scoped_observation_{&mock_observer_};
};

// Operations on the PDM like "adding a profile" asynchronously update the
// database. In such cases, it generally suffices to wait for the operation to
// complete using `PersonalDataProfileTaskWaiter` above.
// But in cases where it is unclear if a profile was added, for example during
// a form submission in browser tests, this doesn't work; if no profile was
// added, the `PersonalDataProfileTaskWaiter` would wait forever.
// In this case, `WaitForPendingDBTasks()` can help: It queues a task to the
// WebDataService's SequencedTaskRunner and returns once it has executed,
// implying that all the prior tasks have finished. In the case of form
// submission, if no profile was added, it will thus return immediately.
// If possible, prefer `PersonalDataProfileTaskWaiter`, since it is more
// explicit which event is waited for.
void WaitForPendingDBTasks(AutofillWebDataService& webdata_service);

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_PERSONAL_DATA_MANAGER_TEST_UTILS_H_
