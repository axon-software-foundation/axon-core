"""Optional scikit-learn example. No network dataset/model download is required."""
def train_model(seed):
    from sklearn.datasets import load_iris
    from sklearn.ensemble import RandomForestClassifier
    from sklearn.metrics import accuracy_score
    from sklearn.model_selection import train_test_split

    dataset = load_iris()
    x_train, x_test, y_train, y_test = train_test_split(
        dataset.data, dataset.target, test_size=0.25,
        random_state=seed, stratify=dataset.target,
    )
    model = RandomForestClassifier(n_estimators=50, random_state=seed, n_jobs=1)
    model.fit(x_train, y_train)
    predictions = model.predict(x_test)
    print("Predictions: ", predictions)
    # Explicit conversion: estimators and NumPy objects do not cross interop.
    return {
        "accuracy": float(accuracy_score(y_test, predictions)),
        "predictions": predictions.tolist(),
        "samples": int(len(predictions)),
        "features": list(dataset.feature_names),
    }
