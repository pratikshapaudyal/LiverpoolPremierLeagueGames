import numpy as np  # linear algebra
import pandas as pd  # data processing, CSV file I/O (e.g. pd.read_csv)

test_data = pd.read_csv("test.csv")
train_data = pd.read_csv("train.csv")

# Combine the two datasets  for data preprocessing, then split them before training your model,
# so that your training and test dataset can have consistent format, both without missing values.

combined_train_test = test_data.append(train_data)
combined_train_test.head()

# # How many null values are there?
null_columns = combined_train_test.columns[combined_train_test.isnull().any()]
combined_train_test[null_columns].isnull().sum()

# Fill null values with a 0
combined_train_test.fillna(0)

women = train_data.loc[train_data.Sex == "female"]["Survived"]
rate_women = sum(women) / len(women)

print("{} % women survived".format(rate_women))
# #train_data['Survived'].value_counts(sort=True, normalize=True)

men = train_data.loc[train_data.Sex == "male"]["Survived"]
rate_men = sum(men) / len(men)

print("{} % men survived".format(rate_men))

from sklearn.ensemble import RandomForestClassifier

y = train_data["Survived"]

features = ["Pclass", "Sex", "SibSp", "Parch"]
X = pd.get_dummies(train_data[features])
X_test = pd.get_dummies(test_data[features])

model = RandomForestClassifier(n_estimators=100, max_depth=5, random_state=1)
model.fit(X, y)
predictions = model.predict(X_test)

output = pd.DataFrame({"PassengerId": test_data.PassengerId, "Survived": predictions})
output.to_csv("my_submission.csv", index=False)
