#!/bin/bash

# Vérification des arguments
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <image_name> <destination_directory>"
    exit 1
fi

IMAGE_NAME="$1"
DEST_DIR="$2"
IMAGE_SAFE_NAME=$(echo "$IMAGE_NAME" | tr '/:' '_')
OUTPUT_FILE="$DEST_DIR/${IMAGE_SAFE_NAME}.oci.tar"

# Vérifie que podman est installé
if ! command -v podman &> /dev/null; then
    echo "Erreur : podman n'est pas installé."
    exit 1
fi

# Crée le répertoire si nécessaire
mkdir -p "$DEST_DIR"

# Pull de l'image
echo "📥 Téléchargement de l'image : $IMAGE_NAME"
if ! podman pull "$IMAGE_NAME"; then
    echo "Erreur lors du téléchargement de l'image $IMAGE_NAME"
    exit 1
fi

# Sauvegarde de l'image au format OCI
echo "💾 Sauvegarde de l'image dans : $OUTPUT_FILE"
if ! podman save --format oci-archive -o "$OUTPUT_FILE" "$IMAGE_NAME"; then
    echo "Erreur lors de la sauvegarde de l'image"
    exit 1
fi

echo "✅ Image $IMAGE_NAME sauvegardée au format OCI dans $OUTPUT_FILE"
