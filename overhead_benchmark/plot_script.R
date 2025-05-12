library(tidyverse)

# Création du dataframe
set.seed(123) # Pour la reproductibilité
data <- tibble(
  variante = rep(c("A", "B", "C"), each = 30),
  temps = c(rnorm(30, mean = 10, sd = 1),
            rnorm(30, mean = 9, sd = 1),
            rnorm(30, mean = 15, sd = 1))
)

# Calcul des statistiques
data_summary <- data %>%
  group_by(variante) %>%
  summarize(
    moyenne = mean(temps),
    lower = t.test(temps)$conf.int[1],
    upper = t.test(temps)$conf.int[2]
  )

# Création du graphique
ggplot() +
  # Barres pour la moyenne
  geom_col(data = data_summary,
           aes(x = variante, y = moyenne),
           fill = "#c0c0c0") +

  # Barres d'erreur pour l'intervalle de confiance
  geom_errorbar(data = data_summary,
                aes(x = variante, ymin = lower, ymax = upper),
                width = 0.2) +

  # Points individuels
  geom_jitter(data = data,
              aes(x = variante, y = temps),
              fill = "black", colour = "black",
              width = 0.1, height = 0,
              alpha = 0.3, size = 2) +

  # Personnalisation du graphique
  labs(title = "Moyenne du temps par variante avec intervalle de confiance à 95%",
       x = "Variante",
       y = "Temps (s)") +
  theme_minimal()

ggsave("/tmp/example.png", bg="#ffffff", width=16, height=9, dpi=100)
