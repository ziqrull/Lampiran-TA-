# ============================================================
# SISTEM KLASIFIKASI PANEL SURYA MENGGUNAKAN DECISION TREE
# Penelitian Skripsi
# Parameter: Tegangan (utama) → Cahaya (kedua) → Suhu (terakhir)
# ============================================================

# ==========================
# Import Library
# ==========================
import pandas as pd
import matplotlib.pyplot as plt
import joblib

from sklearn.model_selection import (
    train_test_split,
    StratifiedKFold,
    cross_val_score
)

from sklearn.tree import (
    DecisionTreeClassifier,
    plot_tree,
    export_text
)

from sklearn.metrics import (
    accuracy_score,
    confusion_matrix,
    classification_report,
    ConfusionMatrixDisplay
)

# ============================================================
# MEMBACA DATASET
# ============================================================

print("="*60)
print("MEMBACA DATASET")
print("="*60)

dataset = pd.read_csv(
    r"C:\Users\ADVAN\Downloads\train\data_panel_label_update.csv"
)

print(dataset.head())
print("\nJumlah Data :", len(dataset))
print("\nInformasi Dataset")
print(dataset.info())

# ============================================================
# PREPROCESSING
# ============================================================

print("\n")
print("="*60)
print("PREPROCESSING DATA")
print("="*60)

print("\nMissing Value")
print(dataset.isnull().sum())

# Menghapus Missing Value
dataset = dataset.dropna()

# Menghapus Data Duplikat
dataset = dataset.drop_duplicates()

# Menghapus spasi di awal/akhir label
dataset["label"] = dataset["label"].astype(str).str.strip()

# Menyeragamkan penulisan label
dataset["label"] = dataset["label"].replace({
    "Tidak perlu": "Tidak Perlu",
    "tidak perlu": "Tidak Perlu",
    "Tidak Perlu": "Tidak Perlu",
    "Bersihkan": "Bersihkan"
})

print("\nJumlah Data Setelah Preprocessing :", len(dataset))

print("\nDistribusi Label Setelah Preprocessing")
print(dataset["label"].value_counts())

# ============================================================
# MEMILIH FITUR DAN LABEL
# ============================================================
# Urutan fitur sesuai hierarki pengambilan keputusan:
# 1. tegangan (parameter utama)
# 2. cahaya   (parameter kedua)
# 3. suhu     (parameter terakhir)
# ============================================================

X = dataset[
    [
        "tegangan",   # Parameter utama
        "cahaya",     # Parameter kedua
        "suhu"        # Parameter terakhir
    ]
]

y = dataset["label"]

print("\nDistribusi Label:")
print(y.value_counts())

# ============================================================
# MEMBAGI DATA TRAINING DAN TESTING
# ============================================================

print("\n")
print("="*60)
print("MEMBAGI DATA")
print("="*60)

X_train, X_test, y_train, y_test = train_test_split(
    X,
    y,
    test_size=0.20,
    random_state=42,
    stratify=y
)
print("\nDistribusi Data Training")
print(y_train.value_counts())

print("\nDistribusi Data Testing")
print(y_test.value_counts())

print("Jumlah Data Training :", len(X_train))
print("Jumlah Data Testing  :", len(X_test))

print("\nUrutan kelas pada model:")

# ============================================================
# 5-FOLD CROSS VALIDATION
# ============================================================

print("\n")
print("="*60)
print("5-FOLD CROSS VALIDATION")
print("="*60)

cv_model = DecisionTreeClassifier(
    criterion="gini",
    max_depth=6,
    min_samples_split=8,
    min_samples_leaf=4,
    random_state=42
)

kfold = StratifiedKFold(
    n_splits=5,
    shuffle=True,
    random_state=42
)

cv_scores = cross_val_score(
    cv_model,
    X_train,
    y_train,
    cv=kfold,
    scoring="accuracy"
)

print("\nAccuracy Tiap Fold")
for i, score in enumerate(cv_scores):
    print(f"Fold {i+1} : {score*100:.2f}%")

print("\nRata-rata Accuracy : {:.2f}%".format(cv_scores.mean()*100))
print("Standar Deviasi    : {:.2f}%".format(cv_scores.std()*100))

# ============================================================
# GRAFIK HASIL 5-FOLD
# ============================================================

plt.figure(figsize=(8,5))
plt.plot(
    range(1, 6),
    cv_scores,
    marker='o',
    linewidth=2,
    color='steelblue'
)
plt.xticks(range(1, 6))
plt.xlabel("Fold")
plt.ylabel("Accuracy")
plt.title("5-Fold Cross Validation - Sistem Pembersih Panel Surya")
plt.ylim(0, 1.1)
plt.grid(True)
plt.tight_layout()
plt.savefig("grafik_crossvalidation.png", dpi=200, bbox_inches='tight')
plt.show()

# ============================================================
# MEMBANGUN MODEL DECISION TREE
# ============================================================

print("\n")
print("="*60)
print("TRAINING MODEL")
print("="*60)

model = DecisionTreeClassifier(
    criterion="gini",
    splitter="best",
    max_depth=6,
    min_samples_split=8,
    min_samples_leaf=4,
    max_features=None,
    random_state=42
)

model.fit(X_train, y_train)

print("Training selesai")

print("\nUrutan kelas model:")
print(model.classes_)

# ============================================================
# PREDIKSI
# ============================================================

y_pred = model.predict(X_test)
print("\nPrediksi Setiap Kelas")
print(pd.Series(y_pred).value_counts())

# ============================================================
# AKURASI
# ============================================================

print("\n")
print("="*60)
print("HASIL AKURASI")
print("="*60)

accuracy = accuracy_score(y_test, y_pred)
print("Accuracy :", round(accuracy*100, 2), "%")

# ============================================================
# CLASSIFICATION REPORT
# ============================================================

print("\n")
print("="*60)
print("CLASSIFICATION REPORT")
print("="*60)

print(classification_report(y_test, y_pred))

# ============================================================
# CONFUSION MATRIX
# ============================================================

print("\n")
print("="*60)
print("CONFUSION MATRIX")
print("="*60)

print("\nLabel pada Data Testing")
print(y_test.value_counts())

print("\nLabel Hasil Prediksi")
print(pd.Series(y_pred).value_counts())

label_order = ["Tidak Perlu", "Bersihkan"]

cm = confusion_matrix(
    y_test,
    y_pred,
    labels=label_order
)

print("\nConfusion Matrix")
print(cm)

tn, fp, fn, tp = cm.ravel()

print("\n===== DETAIL CONFUSION MATRIX =====")
print("True Negative  (TN) :", tn)
print("False Positive (FP) :", fp)
print("False Negative (FN) :", fn)
print("True Positive  (TP) :", tp)

accuracy_m = (tp + tn) / (tp + tn + fp + fn)

precision_m = tp / (tp + fp) if (tp + fp) != 0 else 0

recall_m = tp / (tp + fn) if (tp + fn) != 0 else 0

f1_m = (
    2 * precision_m * recall_m /
    (precision_m + recall_m)
    if (precision_m + recall_m) != 0
    else 0
)

print("\n===== METRIK =====")
print(f"Accuracy  : {accuracy_m*100:.2f}%")
print(f"Precision : {precision_m*100:.2f}%")
print(f"Recall    : {recall_m*100:.2f}%")
print(f"F1 Score  : {f1_m*100:.2f}%")

disp = ConfusionMatrixDisplay(
    confusion_matrix=cm,
    display_labels=label_order
)

disp.plot(
    cmap="Blues",
    values_format="d"
)

plt.title("Confusion Matrix - Sistem Pembersih Panel Surya")
plt.tight_layout()
plt.savefig(
    "confusion_matrix_panel_surya.png",
    dpi=300
)
plt.show()

# ============================================================
# VISUALISASI DECISION TREE
# ============================================================

print("\n")
print("="*60)
print("VISUALISASI DECISION TREE")
print("="*60)

plt.figure(figsize=(40, 20))
plot_tree(
    model,
    feature_names=["tegangan", "cahaya", "suhu"],
    class_names=model.classes_,
    filled=True,
    rounded=True,
    fontsize=9,
    precision=2
)
plt.title("Visualisasi Decision Tree - Keputusan Pembersihan Panel Surya",
          fontsize=18)
plt.tight_layout()
plt.savefig("decision_tree_panel_surya.png", dpi=200, bbox_inches='tight')
print("Gambar pohon disimpan: 'decision_tree_panel_surya.png'")
plt.show()

# ============================================================
# FEATURE IMPORTANCE
# ============================================================

print("\n")
print("="*60)
print("FEATURE IMPORTANCE")
print("="*60)

importance = pd.DataFrame({
    "Feature"    : ["tegangan", "cahaya", "suhu"],
    "Importance" : model.feature_importances_
})
importance = importance.sort_values(by="Importance", ascending=False)
print(importance.to_string(index=False))

plt.figure(figsize=(7, 5))
plt.bar(
    importance["Feature"],
    importance["Importance"],
    color=['steelblue', 'orange', 'green']
)
plt.title("Feature Importance - Parameter Keputusan Pembersihan")
plt.xlabel("Parameter")
plt.ylabel("Importance")
plt.tight_layout()
plt.savefig("feature_importance.png", dpi=200, bbox_inches='tight')
plt.show()

# ============================================================
# RULE EXTRACTION (untuk konversi ke ESP32)
# ============================================================

print("\n")
print("="*60)
print("RULE HASIL DECISION TREE (UNTUK KONVERSI KE ESP32)")
print("="*60)

rules = export_text(
    model,
    feature_names=["tegangan", "cahaya", "suhu"]
)
print(model.classes_)
print(rules)

with open("struktur_decision_tree.txt", "w", encoding="utf-8") as f:

    f.write("=================================================\n")
    f.write("RULE HASIL DECISION TREE\n")
    f.write("=================================================\n\n")

    f.write("Feature:\n")
    f.write("- Tegangan\n")
    f.write("- Cahaya\n")
    f.write("- Suhu\n\n")

    f.write(rules)
print("Struktur pohon disimpan: 'struktur_decision_tree.txt'")

# ============================================================
# MENYIMPAN MODEL
# ============================================================

joblib.dump(model, "DecisionTree_Model.pkl")
print("\nModel berhasil disimpan: 'DecisionTree_Model.pkl'")

# ============================================================
# CONTOH PREDIKSI DATA BARU
# ============================================================

print("\n")
print("="*60)
print("PREDIKSI DATA BARU")
print("="*60)

# Contoh 1: Panel kotor (tegangan rendah, cahaya tinggi, suhu tinggi)
# Harusnya → Bersihkan
data_baru_1 = pd.DataFrame({
    "tegangan" : [14.5],
    "cahaya"   : [35000.0],
    "suhu"     : [38.5]
})

# Contoh 2: Panel bersih (tegangan normal, cahaya tinggi, suhu tinggi)
# Harusnya → Tidak Perlu
data_baru_2 = pd.DataFrame({
    "tegangan" : [18.5],
    "cahaya"   : [30000.0],
    "suhu"     : [38.0]
})

print("\nContoh Prediksi:")
print(f"  Data 1 (panel kotor) → {model.predict(data_baru_1)[0]}")
print(f"  Data 2 (panel bersih) → {model.predict(data_baru_2)[0]}")

print("\n")
print("="*60)
print("PROGRAM SELESAI")
print("="*60)