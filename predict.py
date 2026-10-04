import pickle
import pandas as pd
import os


# Load the trained Random Forest model
model_path = os.path.join(
    os.path.dirname(__file__),
    "model.pkl"
)

with open(model_path, "rb") as file:
    model = pickle.load(file)

def predict_risk(
    miss_distance_m,
    relative_speed_km_s,
    time_to_tca_sec,
    cov_trace_m2
):
    features = pd.DataFrame([{
        "miss_distance_m": miss_distance_m,
        "relative_speed_km_s": relative_speed_km_s,
        "time_to_tca_sec": time_to_tca_sec,
        "cov_trace_m2": cov_trace_m2
    }])

    prediction = model.predict(features)

    return prediction[0]


# Test the prediction function
if __name__ == "__main__":
    result = predict_risk(
        500,
        7.5,
        120,
        25
    )

    print("Predicted Risk Level:", result)